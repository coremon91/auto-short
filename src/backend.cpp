#include "backend.hpp"

#include "build_config.hpp"
#include "mock_backend.hpp"

#if XDCAM_HAVE_DECKLINK
#include "decklink_backend.hpp"
#endif

namespace xdcam {

std::unique_ptr<DeviceBackend> OpenBackend(BackendKind kind, const std::string& fixture, std::string& error) {
    if (kind == BackendKind::Mock) {
        return MakeMockBackend(fixture);
    }
#if XDCAM_HAVE_DECKLINK
    if (kind == BackendKind::DeckLink) {
        return MakeDeckLinkBackend();
    }
#else
    (void)fixture;
#endif
    error = std::string(XDCAM_DECKLINK_NOTE);
    return nullptr;
}

}  // namespace xdcam
