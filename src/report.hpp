#pragma once

#include "device.hpp"
#include "preflight.hpp"
#include "runtime_info.hpp"

#include <string>

namespace xdcam {

struct RunInfo {
    BuildInfo build;
    std::string backend_requested;
    std::string backend_selection;
};

std::string FormatReport(const ScanResult& scan, const RunInfo& run, const PreflightResult* preflight);

}  // namespace xdcam
