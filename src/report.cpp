#include "report.hpp"

#include "mock_backend.hpp"
#include "util.hpp"

#include <sstream>

namespace xdcam {
namespace {

void Field(std::ostream& out, int indent, const std::string& key, const std::string& value) {
    out << std::string(static_cast<std::size_t>(indent) * 2, ' ') << key;
    if (!value.empty()) {
        out << ": " << value;
    }
    out << '\n';
}

std::string OrNone(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "none";
    }
    return Join(values, ",");
}

std::string OrUnavailable(const std::string& value) { return value.empty() ? "unavailable" : value; }

void WriteDevice(std::ostream& out, int index, const DeviceInfo& device) {
    Field(out, 0, "device[" + std::to_string(index) + "]", "");
    Field(out, 1, "display_name", device.display_name);
    Field(out, 1, "persistent_id", device.persistent_id_available ? device.persistent_id : "unavailable");
    Field(out, 1, "model_name", OrUnavailable(device.model_name));
    Field(out, 1, "device_interface", OrUnavailable(device.device_interface));
    Field(out, 1, "device_group_id", OrUnavailable(device.device_group_id));
    Field(out, 1, "video_io", OrNone(device.video_io));
    Field(out, 1, "active_profile", OrUnavailable(device.active_profile));
    Field(out, 1, "profile_list", OrUnavailable(device.profile_list));
    Field(out, 1, "duplex", OrUnavailable(device.duplex));
    Field(out, 1, "in_use", BoolText(device.in_use));
    Field(out, 1, "busy_detail", device.busy_detail.empty() ? "none" : device.busy_detail);
    Field(out, 1, "input_connectors", OrNone(device.input_connectors));
    Field(out, 1, "output_connectors", OrNone(device.output_connectors));
    Field(out, 1, "active_input_connector", OrUnavailable(device.active_input_connector));
    Field(out, 1, "audio_input", OrNone(device.audio_input));
    Field(out, 1, "max_audio_channels",
          device.max_audio_channels < 0 ? "unavailable" : std::to_string(device.max_audio_channels));
    Field(out, 1, "audio_8ch_supported", BoolText(device.audio_8ch_supported));
    Field(out, 1, "audio_8ch_openable", BoolText(device.audio_8ch_openable));
    Field(out, 1, "subdevice_index", device.subdevice_index < 0 ? "unavailable" : std::to_string(device.subdevice_index));
    Field(out, 1, "number_of_subdevices",
          device.number_of_subdevices < 1 ? "unavailable" : std::to_string(device.number_of_subdevices));
    Field(out, 1, "pixel_formats", OrNone(device.pixel_formats));
    Field(out, 1, "display_mode_count", std::to_string(device.display_modes.size()));
    for (std::size_t i = 0; i < device.display_modes.size(); ++i) {
        const DisplayModeInfo& mode = device.display_modes[i];
        Field(out, 1, "display_mode[" + std::to_string(i) + "]", "");
        Field(out, 2, "name", mode.name);
        Field(out, 2, "width", std::to_string(mode.width));
        Field(out, 2, "height", std::to_string(mode.height));
        Field(out, 2, "frame_rate", std::to_string(mode.frame_rate_num) + "/" + std::to_string(mode.frame_rate_den));
        Field(out, 2, "field_dominance", mode.field_dominance);
        Field(out, 2, "pixel_formats", OrNone(mode.pixel_formats));
    }
    Field(out, 1, "input_instance_count", std::to_string(device.inputs.size()));
    for (std::size_t i = 0; i < device.inputs.size(); ++i) {
        const InputInstanceInfo& input = device.inputs[i];
        Field(out, 1, "input[" + std::to_string(i) + "]", "");
        Field(out, 2, "subdevice_index", std::to_string(input.subdevice_index));
        Field(out, 2, "simultaneous_capture_openable", BoolText(input.simultaneous_capture_openable));
        Field(out, 2, "open_probe", input.open_probe);
        Field(out, 2, "streams_started", "false");
    }
}

}  // namespace

std::string FormatReport(const ScanResult& scan, const RunInfo& run, const PreflightResult* preflight) {
    std::ostringstream out;
    if (preflight != nullptr && preflight->four_channel_refused) {
        out << kFourIndependentInputsRefusal << '\n';
    }
    out << "XDCAM phase 0 device scan\n";
    Field(out, 0, "phase", "0");
    Field(out, 0, "scope", "device scan and record-start preflight only");
    Field(out, 0, "capture_implemented", "false");
    Field(out, 0, "mxf_implemented", "false");
    Field(out, 0, "capture_started", "false");
    Field(out, 0, "mxf_started", "false");
    Field(out, 0, "hardware_run", "not claimed");
    Field(out, 0, "hardware_pass", "false");
    Field(out, 0, "qualification", "not a hardware pass");
    const bool hardware_observed = !scan.mock && scan.error.empty() && !scan.devices.empty();
    Field(out, 0, "hardware_observed", BoolText(hardware_observed));
    Field(out, 0, "compiler", run.build.compiler);
    Field(out, 0, "cmake", run.build.cmake);
    Field(out, 0, "cxx_standard", run.build.cxx_standard);
    Field(out, 0, "ffmpeg", run.build.ffmpeg);
    Field(out, 0, "ffprobe", run.build.ffprobe);
    Field(out, 0, "mediainfo", run.build.mediainfo);
    Field(out, 0, "decklink_backend_built", BoolText(run.build.decklink_backend_built));
    Field(out, 0, "decklink_build_note", run.build.decklink_build_note);
    Field(out, 0, "backend_requested", run.backend_requested);
    Field(out, 0, "backend", scan.backend.empty() ? "none" : scan.backend);
    Field(out, 0, "backend_selection", run.backend_selection);
    Field(out, 0, "mock", BoolText(scan.mock));
    if (scan.mock) {
        Field(out, 0, "fixture", scan.fixture);
        Field(out, 0, "mock_notice", scan.notice.empty() ? std::string(kMockNotice) : scan.notice);
    }
    if (!scan.decklink_header_version.empty()) {
        Field(out, 0, "decklink_header_version", scan.decklink_header_version);
    }
    if (!scan.decklink_runtime_version.empty()) {
        Field(out, 0, "decklink_runtime_version", scan.decklink_runtime_version);
    }
    if (!scan.decklink_driver_note.empty()) {
        Field(out, 0, "decklink_driver_note", scan.decklink_driver_note);
    }
    if (!scan.error.empty()) {
        Field(out, 0, "scan_error", scan.error);
    }
    Field(out, 0, "device_count", std::to_string(scan.devices.size()));
    Field(out, 0, "independent_inputs", std::to_string(scan.independent_inputs));
    const int from_scan = scan.independent_inputs > 4 ? 4 : scan.independent_inputs;
    Field(out, 0, "channel_count_from_scan", std::to_string(from_scan));
    Field(out, 0, "four_channel_record_available", BoolText(scan.independent_inputs >= 4));
    for (std::size_t i = 0; i < scan.devices.size(); ++i) {
        WriteDevice(out, static_cast<int>(i), scan.devices[i]);
    }
    if (preflight != nullptr) {
        Field(out, 0, "requested_channels", std::to_string(preflight->requested_channels));
        Field(out, 0, "channel_count_source", preflight->channel_count_source);
        Field(out, 0, "check_channel_count", preflight->channel_count_ok ? "pass" : "fail");
        if (!preflight->channel_count_detail.empty()) {
            Field(out, 0, "check_channel_count_detail", preflight->channel_count_detail);
        }
        Field(out, 0, "check_path", preflight->path_ok ? "pass" : "fail");
        if (!preflight->path_resolved.empty()) {
            Field(out, 0, "path", preflight->path_resolved);
        }
        if (!preflight->path_detail.empty()) {
            Field(out, 0, "check_path_detail", preflight->path_detail);
        }
        Field(out, 0, "check_free_space", preflight->free_space_ok ? "pass" : "fail");
        if (preflight->free_bytes_known) {
            Field(out, 0, "free_bytes", std::to_string(preflight->free_bytes));
        }
        if (!preflight->free_space_detail.empty()) {
            Field(out, 0, "check_free_space_detail", preflight->free_space_detail);
        }
        Field(out, 0, "check_duplicate_input", preflight->duplicate_ok ? "pass" : "fail");
        if (!preflight->duplicate_detail.empty()) {
            Field(out, 0, "check_duplicate_input_detail", preflight->duplicate_detail);
        }
        Field(out, 0, "check_device_busy", preflight->busy_ok ? "pass" : "fail");
        if (!preflight->busy_detail.empty()) {
            Field(out, 0, "check_device_busy_detail", preflight->busy_detail);
        }
        const char* audio_state = "fail";
        if (!preflight->audio_detail.empty() && preflight->audio_detail.compare(0, 7, "skipped") == 0) {
            audio_state = "skipped";
        } else if (preflight->audio_ok) {
            audio_state = "pass";
        }
        Field(out, 0, "check_audio_8ch", audio_state);
        if (!preflight->audio_detail.empty()) {
            Field(out, 0, "check_audio_8ch_detail", preflight->audio_detail);
        }
        Field(out, 0, "single_input_record_possible", BoolText(preflight->single_input_record_possible));
        Field(out, 0, "files_planned", std::to_string(preflight->files_planned));
        Field(out, 0, "files_written", "0");
        Field(out, 0, "assignment_count", std::to_string(preflight->assignments.size()));
        for (std::size_t i = 0; i < preflight->assignments.size(); ++i) {
            const Assignment& assignment = preflight->assignments[i];
            Field(out, 0, "assignment[" + std::to_string(i) + "]",
                  assignment.id + " persistent_id=" + assignment.persistent_id +
                      " subdevice=" + std::to_string(assignment.subdevice_index) + " name=" + assignment.display_name);
        }
        Field(out, 0, "start_allowed", BoolText(preflight->start_allowed));
        Field(out, 0, "record_started", "false");
    }
    return out.str();
}

}  // namespace xdcam
