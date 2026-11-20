#include "preflight.hpp"

#include <filesystem>
#include <set>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace xdcam {
namespace {

struct ResolvedInput {
    const DeviceInfo* device = nullptr;
    const InputInstanceInfo* input = nullptr;
};

std::vector<ResolvedInput> OpenableInputs(const ScanResult& scan) {
    std::vector<ResolvedInput> inputs;
    for (const DeviceInfo& device : scan.devices) {
        for (const InputInstanceInfo& input : device.inputs) {
            if (input.simultaneous_capture_openable) {
                inputs.push_back(ResolvedInput{&device, &input});
            }
        }
    }
    return inputs;
}

int FindDeviceIndex(const ScanResult& scan, const std::string& persistent_id, int subdevice_index) {
    for (std::size_t i = 0; i < scan.devices.size(); ++i) {
        const DeviceInfo& device = scan.devices[i];
        if (device.subdevice_index != subdevice_index) {
            continue;
        }
        if (device.persistent_id_available && device.persistent_id == persistent_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const DeviceInfo* DeviceAt(const ScanResult& scan, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= scan.devices.size()) {
        return nullptr;
    }
    return &scan.devices[static_cast<std::size_t>(index)];
}

bool AudioReady(const DeviceInfo& device) {
    return device.audio_8ch_supported && device.audio_8ch_openable && device.max_audio_channels >= 8;
}

std::string ChannelIdForIndex(int index) { return "CH" + std::to_string(index + 1); }

void CheckPath(const Config& config, PreflightResult& result) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path resolved = fs::absolute(config.output_dir, ec);
    if (ec) {
        result.path_ok = false;
        result.path_detail = "path is not valid";
        return;
    }
    result.path_resolved = resolved.string();
    if (!fs::is_directory(resolved, ec) || ec) {
        result.path_ok = false;
        result.path_detail = "path is not an existing directory";
        return;
    }
#if defined(__linux__) || defined(__APPLE__)
    if (access(resolved.c_str(), W_OK) != 0) {
        result.path_ok = false;
        result.path_detail = "path is not writable";
        return;
    }
#endif
    result.path_ok = true;
}

void CheckFreeSpace(const Config& config, PreflightResult& result) {
    if (!result.path_ok) {
        result.free_space_ok = false;
        result.free_space_detail = "free space was not measured because the path check failed";
        return;
    }
#if defined(__linux__) || defined(__APPLE__)
    struct statvfs stats {};
    if (statvfs(result.path_resolved.c_str(), &stats) != 0) {
        result.free_space_ok = false;
        result.free_space_detail = "statvfs failed";
        return;
    }
    result.free_bytes = static_cast<uint64_t>(stats.f_bavail) * static_cast<uint64_t>(stats.f_frsize);
    result.free_bytes_known = true;
    if (result.free_bytes < static_cast<uint64_t>(config.min_free_bytes)) {
        result.free_space_ok = false;
        result.free_space_detail = "free space is below min_free_bytes";
        return;
    }
    result.free_space_ok = true;
#else
    result.free_space_ok = false;
    result.free_space_detail = "free space check is implemented for POSIX in Phase 0";
#endif
}

}  // namespace

PreflightResult RunPreflight(const Config& config, const ScanResult& scan, std::optional<int> channel_override) {
    PreflightResult result;
    const std::vector<ResolvedInput> openable = OpenableInputs(scan);
    const int independent = static_cast<int>(openable.size());

    if (channel_override.has_value()) {
        result.requested_channels = *channel_override;
        result.channel_count_source = "cli";
    } else if (config.channel_count.has_value()) {
        result.requested_channels = *config.channel_count;
        result.channel_count_source = "config";
    } else if (config.has_channels) {
        result.requested_channels = static_cast<int>(config.channels.size());
        result.channel_count_source = "config_channels";
    } else {
        result.requested_channels = independent > 4 ? 4 : independent;
        result.channel_count_source = "scan";
    }

    if (result.requested_channels == 4 && independent < 4) {
        result.four_channel_refused = true;
        result.channel_count_ok = false;
        result.channel_count_detail = "scan found " + std::to_string(independent) + " independent input(s)";
    } else if (result.requested_channels < 1) {
        result.channel_count_ok = false;
        result.channel_count_detail = "scan found no independent input";
    } else if (result.requested_channels > independent) {
        result.channel_count_ok = false;
        result.channel_count_detail = "requested " + std::to_string(result.requested_channels) +
                                      " but scan found " + std::to_string(independent) + " independent input(s)";
    } else {
        result.channel_count_ok = true;
    }

    std::vector<Assignment> explicit_assignments;
    bool explicit_mode = config.has_channels && !channel_override.has_value() && !config.channel_count.has_value();
    if (config.has_channels && config.channel_count.has_value() && !channel_override.has_value()) {
        explicit_mode = true;
    }
    if (channel_override.has_value()) {
        explicit_mode = false;
    }

    if (explicit_mode) {
        std::set<std::string> keys;
        bool duplicate = false;
        for (const ChannelConfig& channel : config.channels) {
            Assignment assignment;
            assignment.id = channel.id;
            assignment.persistent_id = channel.persistent_id;
            assignment.subdevice_index = channel.subdevice_index;
            const std::string key = channel.persistent_id + "#" + std::to_string(channel.subdevice_index);
            if (!keys.insert(key).second) {
                duplicate = true;
            }
            assignment.device_index = FindDeviceIndex(scan, channel.persistent_id, channel.subdevice_index);
            if (const DeviceInfo* device = DeviceAt(scan, assignment.device_index)) {
                assignment.display_name = device->display_name;
            }
            explicit_assignments.push_back(std::move(assignment));
        }
        if (result.channel_count_ok) {
            result.assignments = explicit_assignments;
        }
        result.duplicate_ok = !duplicate;
        if (duplicate) {
            result.duplicate_detail = "the same input is assigned to more than one channel";
        }
    } else if (result.channel_count_ok) {
        for (int i = 0; i < result.requested_channels; ++i) {
            Assignment assignment;
            assignment.id = ChannelIdForIndex(i);
            assignment.persistent_id = openable[static_cast<std::size_t>(i)].device->persistent_id_available
                                            ? openable[static_cast<std::size_t>(i)].device->persistent_id
                                            : std::string("unavailable");
            assignment.subdevice_index = openable[static_cast<std::size_t>(i)].device->subdevice_index;
            assignment.display_name = openable[static_cast<std::size_t>(i)].device->display_name;
            assignment.device_index =
                static_cast<int>(openable[static_cast<std::size_t>(i)].device - scan.devices.data());
            result.assignments.push_back(std::move(assignment));
        }
        result.duplicate_ok = true;
    } else {
        result.duplicate_ok = true;
        result.assignments.clear();
    }

    if (explicit_mode && result.channel_count_ok) {
        bool missing = false;
        bool busy = false;
        bool closed = false;
        for (const Assignment& assignment : result.assignments) {
            const DeviceInfo* device = DeviceAt(scan, assignment.device_index);
            if (device == nullptr) {
                missing = true;
                continue;
            }
            if (device->in_use) {
                busy = true;
            }
            bool openable_match = false;
            for (const InputInstanceInfo& input : device->inputs) {
                if (input.subdevice_index == device->subdevice_index && input.simultaneous_capture_openable) {
                    openable_match = true;
                }
            }
            if (!openable_match) {
                closed = true;
            }
        }
        if (missing) {
            result.busy_ok = false;
            result.busy_detail = "an assigned input was not found in the scan";
        } else if (busy) {
            result.busy_ok = false;
            result.busy_detail = "an assigned device is in use";
        } else if (closed) {
            result.busy_ok = false;
            result.busy_detail = "an assigned input cannot be opened for capture";
        } else {
            result.busy_ok = true;
        }
    } else if (!explicit_mode && result.assignments.empty()) {
        bool any_busy = false;
        for (const DeviceInfo& device : scan.devices) {
            if (device.in_use) {
                any_busy = true;
            }
        }
        if (any_busy && independent == 0) {
            result.busy_ok = false;
            result.busy_detail = "device is in use and capture was not opened";
        } else if (!result.channel_count_ok) {
            result.busy_ok = true;
        } else {
            result.busy_ok = true;
        }
    } else {
        result.busy_ok = true;
    }

    if (result.assignments.empty()) {
        result.audio_ok = true;
        if (!result.channel_count_ok || !result.duplicate_ok) {
            result.audio_detail = "skipped because no input was assigned";
        }
    } else {
        bool audio_bad = false;
        for (const Assignment& assignment : result.assignments) {
            const DeviceInfo* device = DeviceAt(scan, assignment.device_index);
            if (device == nullptr || !AudioReady(*device)) {
                audio_bad = true;
            }
        }
        result.audio_ok = !audio_bad;
        if (audio_bad) {
            result.audio_detail = "an assigned input does not have openable 8-channel audio";
        }
    }

    if (explicit_mode && !result.duplicate_ok) {
        result.assignments.clear();
    }

    CheckPath(config, result);
    CheckFreeSpace(config, result);

    result.single_input_record_possible = false;
    for (const ResolvedInput& item : openable) {
        if (AudioReady(*item.device)) {
            result.single_input_record_possible = true;
            break;
        }
    }

    result.start_allowed = result.channel_count_ok && result.path_ok && result.free_space_ok && result.duplicate_ok &&
                           result.busy_ok && result.audio_ok && !result.four_channel_refused;
    result.files_planned = result.start_allowed ? result.requested_channels : 0;
    if (!result.start_allowed) {
        result.assignments.clear();
    }
    return result;
}

}  // namespace xdcam
