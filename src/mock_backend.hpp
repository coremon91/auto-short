#pragma once

#include "backend.hpp"

#include <memory>
#include <string>

namespace xdcam {

inline constexpr const char* kMockNotice =
    "deterministic mock fixture; not a DeckLink 4K Extreme result; not a hardware pass";

std::unique_ptr<DeviceBackend> MakeMockBackend(std::string fixture);

}  // namespace xdcam
