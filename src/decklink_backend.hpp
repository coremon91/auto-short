#pragma once

#include "backend.hpp"

#include <memory>

namespace xdcam {

std::unique_ptr<DeviceBackend> MakeDeckLinkBackend();

}  // namespace xdcam
