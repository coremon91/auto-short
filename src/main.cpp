#include "backend.hpp"
#include "build_config.hpp"
#include "config.hpp"
#include "preflight.hpp"
#include "report.hpp"
#include "runtime_info.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace {

void PrintHelp(std::ostream& out) {
    out << "xdcam-scan scan [--backend mock|decklink|auto] [--fixture NAME] [--config FILE]\n"
           "xdcam-scan preflight --config FILE [--backend mock|decklink|auto] [--fixture NAME] [--channels N]\n"
           "\n"
           "Phase 0 lists DeckLink inputs and decides whether a record start would be allowed.\n"
           "It does not capture, encode, or write MXF.\n"
           "When channel_count is omitted, the start uses the scan's independent input count\n"
           "(1 now, up to 4 after a card swap). A request for 4 inputs is refused with\n"
           "\"4 INDEPENDENT INPUTS NOT AVAILABLE\" when fewer than 4 can be opened together.\n"
           "That refusal does not block a later 1-channel start, and one input is never\n"
           "duplicated into extra files.\n"
           "The mock backend is a deterministic fixture. It is not a DeckLink result and\n"
           "not a hardware pass.\n"
           "\n"
           "Exit codes: 0 scan or allowed preflight, 1 usage or rejected config,\n"
           "2 preflight refused, 3 backend scan failed.\n";
}

bool ParseChannels(const std::string& text, int& value) {
    if (text.size() != 1 || text[0] < '1' || text[0] > '4') {
        return false;
    }
    value = text[0] - '0';
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintHelp(std::cerr);
        return 1;
    }
    const std::string command = argv[1];
    if (command == "--help" || command == "-h" || command == "help") {
        PrintHelp(std::cout);
        return 0;
    }
    if (command != "scan" && command != "preflight") {
        std::cerr << "unknown command\n";
        PrintHelp(std::cerr);
        return 1;
    }

    std::string config_path;
    std::string backend_arg;
    std::string fixture_arg;
    bool backend_set = false;
    bool fixture_set = false;
    std::optional<int> channels;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--config") {
            const char* value = need("--config");
            if (value == nullptr) {
                return 1;
            }
            config_path = value;
        } else if (arg == "--backend") {
            const char* value = need("--backend");
            if (value == nullptr) {
                return 1;
            }
            backend_arg = value;
            backend_set = true;
        } else if (arg == "--fixture") {
            const char* value = need("--fixture");
            if (value == nullptr) {
                return 1;
            }
            fixture_arg = value;
            fixture_set = true;
        } else if (arg == "--channels") {
            const char* value = need("--channels");
            if (value == nullptr) {
                return 1;
            }
            int parsed = 0;
            if (!ParseChannels(value, parsed)) {
                std::cerr << "unsupported value for --channels\n";
                return 1;
            }
            channels = parsed;
        } else if (arg == "--help" || arg == "-h") {
            PrintHelp(std::cout);
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 1;
        }
    }

    if ((backend_set && backend_arg != "mock" && backend_arg != "decklink" && backend_arg != "auto") ||
        (fixture_set && fixture_arg.empty())) {
        std::cerr << "unsupported value for --backend or --fixture\n";
        return 1;
    }
    if (channels.has_value() && config_path.empty()) {
        std::cerr << "--channels requires --config\n";
        return 1;
    }

    xdcam::Config config;
    const bool have_config = !config_path.empty();
    if (have_config) {
        std::string error;
        if (!xdcam::LoadConfigFile(config_path, config, error)) {
            std::cerr << "CONFIG_REJECTED: " << error << "\n";
            return 1;
        }
    } else if (command == "preflight") {
        std::cerr << "preflight requires --config\n";
        return 1;
    }

    std::string requested = "auto";
    if (backend_set) {
        requested = backend_arg;
    } else if (have_config) {
        requested = config.backend;
    }

    const bool decklink_built = XDCAM_HAVE_DECKLINK != 0;
    xdcam::BackendKind kind = xdcam::BackendKind::Mock;
    std::string selection;
    if (requested == "decklink") {
        kind = xdcam::BackendKind::DeckLink;
        selection = "DeckLink backend selected";
    } else if (requested == "mock") {
        kind = xdcam::BackendKind::Mock;
        selection = "mock backend selected";
    } else if (decklink_built) {
        kind = xdcam::BackendKind::DeckLink;
        selection = "auto selected DeckLink because the backend was built";
    } else {
        kind = xdcam::BackendKind::Mock;
        selection = "auto selected mock because the DeckLink backend was left out of this build";
    }

    const bool fixture_specified = fixture_set || (have_config && config.has_fixture);
    if (kind == xdcam::BackendKind::DeckLink && fixture_specified) {
        std::cerr << "CONFIG_REJECTED: fixture is only valid with the mock backend\n";
        return 1;
    }
    if (kind == xdcam::BackendKind::DeckLink && !decklink_built) {
        std::cerr << "DeckLink backend is not in this build: " << XDCAM_DECKLINK_NOTE << "\n";
        return 1;
    }

    std::string fixture = "one-input";
    if (fixture_set) {
        fixture = fixture_arg;
    } else if (have_config && config.has_fixture) {
        fixture = config.fixture;
    }

    std::string open_error;
    const std::unique_ptr<xdcam::DeviceBackend> backend = xdcam::OpenBackend(kind, fixture, open_error);
    if (!backend) {
        std::cerr << "DeckLink backend is not in this build: " << open_error << "\n";
        return 1;
    }

    xdcam::ScanResult scan = backend->Scan();
    xdcam::RunInfo run;
    run.build = xdcam::CollectBuildInfo();
    run.backend_requested = requested;
    run.backend_selection = selection;
    if (!scan.error.empty()) {
        std::cout << xdcam::FormatReport(scan, run, nullptr);
        return 3;
    }
    if (!have_config) {
        std::cout << xdcam::FormatReport(scan, run, nullptr);
        return 0;
    }
    const xdcam::PreflightResult preflight = xdcam::RunPreflight(config, scan, channels);
    std::cout << xdcam::FormatReport(scan, run, &preflight);
    return preflight.start_allowed ? 0 : 2;
}
