#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xdcam {

struct ChannelConfig {
    std::string id;
    std::string persistent_id;
    int subdevice_index = 0;
};

struct Config {
    std::string backend = "auto";
    std::optional<int> channel_count;
    bool has_channels = false;
    std::vector<ChannelConfig> channels;
    std::string output_dir;
    int64_t min_free_bytes = 0;
    std::string on_signal_loss;
    bool has_fixture = false;
    std::string fixture;
};

bool LoadConfigString(const std::string& text, Config& config, std::string& error);
bool LoadConfigFile(const std::string& path, Config& config, std::string& error);

}  // namespace xdcam
