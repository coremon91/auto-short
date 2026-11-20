#pragma once

#include <string>

namespace xdcam {

struct BuildInfo {
    std::string compiler;
    std::string cmake;
    std::string cxx_standard = "17";
    std::string ffmpeg = "not present";
    std::string ffprobe = "not present";
    std::string mediainfo = "not present";
    bool decklink_backend_built = false;
    std::string decklink_build_note;
};

BuildInfo CollectBuildInfo();

}  // namespace xdcam
