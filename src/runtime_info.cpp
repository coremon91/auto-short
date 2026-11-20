#include "runtime_info.hpp"

#include "build_config.hpp"
#include "util.hpp"

#include <cstdio>
#include <string>

namespace xdcam {
namespace {

std::string FirstLine(const char* command) {
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return "not present";
    }
    char buffer[512];
    std::string line;
    if (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
        line = TrimRight(buffer);
    }
    while (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
    }
    const int status = pclose(pipe);
    if (line.empty() || status != 0) {
        return "not present";
    }
    return line;
}

}  // namespace

BuildInfo CollectBuildInfo() {
    BuildInfo info;
    info.compiler = std::string(XDCAM_COMPILER_ID) + " " + XDCAM_COMPILER_VERSION;
    info.cmake = XDCAM_CMAKE_VERSION;
    info.cxx_standard = "17";
    info.ffmpeg = FirstLine("ffmpeg -version 2>/dev/null");
    info.ffprobe = FirstLine("ffprobe -version 2>/dev/null");
    info.mediainfo = FirstLine("mediainfo --Version 2>/dev/null");
    info.decklink_backend_built = XDCAM_HAVE_DECKLINK != 0;
    info.decklink_build_note = XDCAM_DECKLINK_NOTE;
    return info;
}

}  // namespace xdcam
