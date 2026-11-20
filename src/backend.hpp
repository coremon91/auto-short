#pragma once

#include "device.hpp"

#include <memory>
#include <string>

namespace xdcam {

enum class BackendKind { Mock, DeckLink };

class DeviceBackend {
   public:
    virtual ~DeviceBackend() = default;
    virtual ScanResult Scan() = 0;
};

std::unique_ptr<DeviceBackend> OpenBackend(BackendKind kind, const std::string& fixture, std::string& error);

}  // namespace xdcam
