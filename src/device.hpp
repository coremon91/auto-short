#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xdcam {

struct DisplayModeInfo {
    std::string name;
    int width = 0;
    int height = 0;
    int64_t frame_rate_num = 0;
    int64_t frame_rate_den = 1;
    std::string field_dominance;
    std::vector<std::string> pixel_formats;
};

struct InputInstanceInfo {
    int subdevice_index = 0;
    bool simultaneous_capture_openable = false;
    std::string open_probe;
    bool streams_started = false;
};

struct DeviceInfo {
    std::string display_name;
    bool persistent_id_available = false;
    std::string persistent_id;
    std::string model_name;
    std::string device_interface;
    std::string device_group_id;
    std::vector<std::string> video_io;
    std::string active_profile;
    std::string profile_list;
    std::string duplex;
    bool in_use = false;
    std::string busy_detail;
    std::vector<std::string> input_connectors;
    std::vector<std::string> output_connectors;
    std::string active_input_connector;
    std::vector<std::string> audio_input;
    int64_t max_audio_channels = -1;
    bool audio_8ch_supported = false;
    bool audio_8ch_openable = false;
    int subdevice_index = -1;
    int number_of_subdevices = -1;
    std::vector<DisplayModeInfo> display_modes;
    std::vector<std::string> pixel_formats;
    std::vector<InputInstanceInfo> inputs;
};

struct ScanResult {
    std::string backend;
    bool mock = false;
    std::string fixture;
    std::string notice;
    std::string error;
    std::string decklink_header_version;
    std::string decklink_runtime_version;
    std::string decklink_driver_note;
    std::vector<DeviceInfo> devices;
    int independent_inputs = 0;
};

inline int CountIndependentInputs(const std::vector<DeviceInfo>& devices) {
    int count = 0;
    for (const DeviceInfo& device : devices) {
        for (const InputInstanceInfo& input : device.inputs) {
            if (input.simultaneous_capture_openable) {
                ++count;
            }
        }
    }
    return count;
}

inline std::string DeviceKey(const DeviceInfo& device, int subdevice_index) {
    const std::string& id = device.persistent_id_available ? device.persistent_id : device.display_name;
    return id + "#" + std::to_string(subdevice_index);
}

}  // namespace xdcam
