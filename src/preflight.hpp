#pragma once

#include "config.hpp"
#include "device.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xdcam {

inline constexpr const char* kFourIndependentInputsRefusal = "4 INDEPENDENT INPUTS NOT AVAILABLE";

struct Assignment {
    std::string id;
    std::string persistent_id;
    int subdevice_index = 0;
    std::string display_name;
    int device_index = -1;
};

struct PreflightResult {
    int requested_channels = 0;
    std::string channel_count_source;
    bool four_channel_refused = false;
    bool start_allowed = false;
    bool single_input_record_possible = false;
    int files_planned = 0;
    std::vector<Assignment> assignments;
    bool channel_count_ok = false;
    std::string channel_count_detail;
    bool path_ok = false;
    std::string path_detail;
    std::string path_resolved;
    bool free_space_ok = false;
    std::string free_space_detail;
    uint64_t free_bytes = 0;
    bool free_bytes_known = false;
    bool duplicate_ok = false;
    std::string duplicate_detail;
    bool busy_ok = false;
    std::string busy_detail;
    bool audio_ok = false;
    std::string audio_detail;
};

PreflightResult RunPreflight(const Config& config, const ScanResult& scan, std::optional<int> channel_override);

}  // namespace xdcam
