#include "config.hpp"
#include "json.hpp"
#include "mock_backend.hpp"
#include "preflight.hpp"
#include "report.hpp"
#include "runtime_info.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

int g_failed = 0;

void Fail(const char* file, int line, const std::string& message) {
    std::cerr << "FAIL " << file << ":" << line << " " << message << "\n";
    ++g_failed;
}

#define CHECK(cond)                                      \
    do {                                                 \
        if (!(cond)) {                                   \
            Fail(__FILE__, __LINE__, #cond);            \
        }                                                \
    } while (0)

template <typename A, typename B>
void CheckEq(const char* file, int line, const char* expr, const A& left, const B& right) {
    if (!(left == right)) {
        std::ostringstream out;
        out << expr << "\n  left: " << left << "\n  right: " << right;
        Fail(file, line, out.str());
    }
}

#define CHECK_EQ(a, b) CheckEq(__FILE__, __LINE__, #a " == " #b, (a), (b))

std::string BaseConfig(const std::string& dir, const std::string& extra) {
    return std::string("{\n") + "  \"backend\": \"mock\",\n" + "  \"output_dir\": \"" + dir + "\",\n" +
           "  \"min_free_bytes\": 1,\n" + "  \"require_audio_8ch\": true,\n" +
           "  \"on_signal_loss\": \"STOP_WITH_ERROR\",\n" + "  \"video\": {\n" + "    \"width\": 1920,\n" +
           "    \"height\": 1080,\n" + "    \"frame_rate_num\": 30000,\n" + "    \"frame_rate_den\": 1001,\n" +
           "    \"scan\": \"interlaced\",\n" + "    \"field_order\": \"top_field_first\",\n" +
           "    \"pixel_format\": \"yuv422p\",\n" + "    \"bitrate_bps\": 50000000\n" + "  },\n" +
           "  \"audio\": {\n" + "    \"sample_rate\": 48000,\n" + "    \"channels_per_input\": 8,\n" +
           "    \"format\": \"pcm_s24\"\n" + "  }" + extra + "\n}\n";
}

xdcam::Config MustLoad(const std::string& text) {
    xdcam::Config config;
    std::string error;
    if (!xdcam::LoadConfigString(text, config, error)) {
        Fail(__FILE__, __LINE__, error);
    }
    return config;
}

xdcam::ScanResult MustScan(const std::string& fixture) {
    const std::unique_ptr<xdcam::DeviceBackend> backend = xdcam::MakeMockBackend(fixture);
    xdcam::ScanResult scan = backend->Scan();
    if (!scan.error.empty()) {
        Fail(__FILE__, __LINE__, scan.error);
    }
    return scan;
}

bool ContainsLine(const std::string& text, const std::string& line) {
    const std::string needle = line + "\n";
    return text.find(needle) != std::string::npos || text == line;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

int main() {
    using xdcam::kFourIndependentInputsRefusal;
    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() / ("xdcam-p0-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::create_directories(temp);
    const std::string dir = temp.string();

    {
        const xdcam::Value value = xdcam::ParseJson("{\"a\":1,\"b\":true,\"c\":null,\"d\":[\"x\"]}");
        CHECK(value.type == xdcam::Value::Type::Object);
        CHECK(value.Find("a")->i == 1);
        CHECK(value.Find("b")->b);
        CHECK(value.Find("c")->type == xdcam::Value::Type::Null);
        bool threw = false;
        try {
            xdcam::ParseJson("{\"a\":1,}");
        } catch (const xdcam::JsonError&) {
            threw = true;
        }
        CHECK(threw);
        threw = false;
        try {
            xdcam::ParseJson("{\"a\":1,\"a\":2}");
        } catch (const xdcam::JsonError&) {
            threw = true;
        }
        CHECK(threw);
    }

    {
        const xdcam::Config config = MustLoad(BaseConfig(dir, ""));
        CHECK(!config.channel_count.has_value());
        CHECK_EQ(config.backend, "mock");
        std::string error;
        xdcam::Config rejected;
        CHECK(!xdcam::LoadConfigString(BaseConfig(dir, ", \"video\": {\"width\": 1280}"), rejected, error));
        CHECK(xdcam::LoadConfigString(BaseConfig(dir, ""), rejected, error));
        const std::string bad_width = BaseConfig(dir, "");
        std::string mutated = bad_width;
        const std::string::size_type at = mutated.find("1920");
        CHECK(at != std::string::npos);
        mutated.replace(at, 4, "1280");
        CHECK(!xdcam::LoadConfigString(mutated, rejected, error));
        CHECK(error.find("video.width") != std::string::npos);
        mutated = bad_width;
        mutated.replace(mutated.find("STOP_WITH_ERROR"), std::string("STOP_WITH_ERROR").size(), "BLACK_FRAME");
        CHECK(!xdcam::LoadConfigString(mutated, rejected, error));
        CHECK(error.find("on_signal_loss") != std::string::npos);
        CHECK(!xdcam::LoadConfigString(BaseConfig(dir, ", \"channel_count\": 5"), rejected, error));
        CHECK(!xdcam::LoadConfigString(BaseConfig(dir, ", \"channel_count\": 0"), rejected, error));
        CHECK(!xdcam::LoadConfigString(BaseConfig(dir, ", \"deinterlace\": true"), rejected, error));
        CHECK(error.find("unsupported key") != std::string::npos);
        CHECK(!xdcam::LoadConfigString(BaseConfig(dir, ", \"require_audio_8ch\": false"), rejected, error));
        const std::filesystem::path example = std::filesystem::path(XDCAM_SOURCE_DIR) / "config" / "one_channel.json";
        CHECK(xdcam::LoadConfigFile(example.string(), rejected, error));
        CHECK(!rejected.channel_count.has_value());
        const std::filesystem::path four = std::filesystem::path(XDCAM_SOURCE_DIR) / "config" / "four_channel.json";
        CHECK(xdcam::LoadConfigFile(four.string(), rejected, error));
        CHECK(rejected.channel_count.has_value());
        CHECK_EQ(*rejected.channel_count, 4);
    }

    const xdcam::ScanResult one = MustScan("one-input");
    CHECK(one.mock);
    CHECK_EQ(one.independent_inputs, 1);
    CHECK_EQ(one.notice, xdcam::kMockNotice);
    CHECK(one.devices[0].display_name.find("4K Extreme") == std::string::npos);
    CHECK(one.devices[0].inputs[0].simultaneous_capture_openable);
    CHECK(!one.devices[0].inputs[0].streams_started);
    CHECK_EQ(one.devices[0].inputs.size(), 1u);
    CHECK(one.devices[0].audio_8ch_supported);

    const xdcam::ScanResult two = MustScan("two-input");
    CHECK_EQ(two.independent_inputs, 2);
    const xdcam::ScanResult four = MustScan("four-input");
    CHECK_EQ(four.independent_inputs, 4);
    const xdcam::ScanResult busy = MustScan("busy");
    CHECK_EQ(busy.independent_inputs, 0);
    CHECK(busy.devices[0].in_use);
    const xdcam::ScanResult no_audio = MustScan("no-audio-8ch");
    CHECK_EQ(no_audio.independent_inputs, 1);
    CHECK(!no_audio.devices[0].audio_8ch_supported);

    const xdcam::ScanResult missing = xdcam::MakeMockBackend("missing-fixture")->Scan();
    CHECK(!missing.error.empty());

    xdcam::RunInfo run;
    run.build = xdcam::CollectBuildInfo();
    run.backend_requested = "mock";
    run.backend_selection = "mock backend selected";
    const std::string scan_text = xdcam::FormatReport(one, run, nullptr);
    CHECK(ContainsLine(scan_text, "hardware_pass: false"));
    CHECK(ContainsLine(scan_text, "hardware_run: not claimed"));
    CHECK(ContainsLine(scan_text, "capture_started: false"));
    CHECK(ContainsLine(scan_text, "mxf_started: false"));
    CHECK(scan_text.find(kFourIndependentInputsRefusal) == std::string::npos);
    CHECK(!run.build.compiler.empty());
    CHECK(!run.build.cmake.empty());

    {
        const xdcam::Config config = MustLoad(BaseConfig(dir, ""));
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(config, one, std::nullopt);
        CHECK(preflight.start_allowed);
        CHECK(!preflight.four_channel_refused);
        CHECK_EQ(preflight.requested_channels, 1);
        CHECK_EQ(preflight.channel_count_source, "scan");
        CHECK_EQ(preflight.files_planned, 1);
        CHECK_EQ(preflight.assignments.size(), 1u);
        CHECK_EQ(preflight.assignments[0].id, "CH1");
        const std::string text = xdcam::FormatReport(one, run, &preflight);
        CHECK(ContainsLine(text, "start_allowed: true"));
        CHECK(ContainsLine(text, "record_started: false"));
        CHECK(ContainsLine(text, "files_written: 0"));
        CHECK(text.find(kFourIndependentInputsRefusal) == std::string::npos);
    }

    {
        const xdcam::Config config = MustLoad(BaseConfig(dir, ",\n  \"channel_count\": 4"));
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(config, one, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(preflight.four_channel_refused);
        CHECK(preflight.single_input_record_possible);
        CHECK_EQ(preflight.files_planned, 0);
        CHECK(preflight.assignments.empty());
        const std::string text = xdcam::FormatReport(one, run, &preflight);
        CHECK(text.rfind(std::string(kFourIndependentInputsRefusal) + "\n", 0) == 0);
        CHECK(ContainsLine(text, "record_started: false"));
        CHECK(ContainsLine(text, "start_allowed: false"));
    }

    {
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(BaseConfig(dir, ",\n  \"channel_count\": 2")), one, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.four_channel_refused);
        CHECK_EQ(preflight.files_planned, 0);
        CHECK(preflight.assignments.empty());
        CHECK(preflight.single_input_record_possible);
        const std::string text = xdcam::FormatReport(one, run, &preflight);
        CHECK(text.find(kFourIndependentInputsRefusal) == std::string::npos);
    }

    {
        const std::string extra =
            ",\n  \"channel_count\": 2,\n  \"channels\": ["
            "{\"id\":\"CH1\",\"persistent_id\":\"mock-sdi-0001\",\"subdevice_index\":0},"
            "{\"id\":\"CH2\",\"persistent_id\":\"mock-sdi-0001\",\"subdevice_index\":0}]";
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(BaseConfig(dir, extra)), two, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.duplicate_ok);
        CHECK(!preflight.four_channel_refused);
        CHECK_EQ(preflight.files_planned, 0);
    }

    {
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(BaseConfig(dir, "")), busy, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.four_channel_refused);
        CHECK(!preflight.busy_ok);
        CHECK(!preflight.single_input_record_possible);
    }

    {
        const std::string named_busy =
            ",\n  \"channels\": ["
            "{\"id\":\"CH1\",\"persistent_id\":\"mock-sdi-busy\",\"subdevice_index\":0}]";
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(BaseConfig(dir, named_busy)), busy, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.channel_count_ok);
        CHECK(!preflight.four_channel_refused);
        CHECK_EQ(preflight.files_planned, 0);
        CHECK(preflight.assignments.empty());
    }

    {
        const xdcam::ScanResult playback = MustScan("playback-busy");
        CHECK_EQ(playback.independent_inputs, 1);
        CHECK(playback.devices[0].in_use);
        CHECK_EQ(playback.devices[0].busy_detail, "playback");
        CHECK_EQ(playback.devices[0].duplex, "full");
        CHECK(playback.devices[0].inputs[0].simultaneous_capture_openable);
        CHECK(!playback.devices[0].inputs[0].streams_started);

        const xdcam::PreflightResult implicit = xdcam::RunPreflight(MustLoad(BaseConfig(dir, "")), playback, std::nullopt);
        CHECK(implicit.start_allowed);
        CHECK(implicit.busy_ok);
        CHECK_EQ(implicit.requested_channels, 1);
        CHECK_EQ(implicit.channel_count_source, "scan");
        CHECK_EQ(implicit.files_planned, 1);
        CHECK_EQ(implicit.assignments.size(), 1u);

        const std::string named =
            ",\n  \"channels\": ["
            "{\"id\":\"CH1\",\"persistent_id\":\"mock-sdi-playback\",\"subdevice_index\":0}]";
        const xdcam::PreflightResult explicit_assign =
            xdcam::RunPreflight(MustLoad(BaseConfig(dir, named)), playback, std::nullopt);
        CHECK(explicit_assign.start_allowed);
        CHECK(explicit_assign.busy_ok);
        CHECK(explicit_assign.busy_detail.empty());
        CHECK_EQ(explicit_assign.channel_count_source, "config_channels");
        CHECK_EQ(explicit_assign.requested_channels, 1);
        CHECK_EQ(explicit_assign.files_planned, 1);
        CHECK_EQ(explicit_assign.assignments.size(), 1u);
        CHECK_EQ(explicit_assign.assignments[0].id, "CH1");
        CHECK_EQ(explicit_assign.assignments[0].persistent_id, "mock-sdi-playback");
        CHECK(!explicit_assign.four_channel_refused);
        const std::string text = xdcam::FormatReport(playback, run, &explicit_assign);
        CHECK(ContainsLine(text, "start_allowed: true"));
        CHECK(ContainsLine(text, "check_device_busy: pass"));
        CHECK(ContainsLine(text, "in_use: true"));
        CHECK(ContainsLine(text, "busy_detail: playback"));
        CHECK(ContainsLine(text, "record_started: false"));
        CHECK(ContainsLine(text, "capture_started: false"));
        CHECK(text.find(kFourIndependentInputsRefusal) == std::string::npos);
    }

    {
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(BaseConfig(dir, "")), no_audio, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.audio_ok);
        CHECK(!preflight.four_channel_refused);
        CHECK_EQ(preflight.files_planned, 0);
    }

    {
        const xdcam::PreflightResult preflight =
            xdcam::RunPreflight(MustLoad(BaseConfig(dir + "/missing", "")), one, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.path_ok);
        CHECK(!preflight.four_channel_refused);
    }

    {
        std::string huge = BaseConfig(dir, "");
        const std::string needle = "\"min_free_bytes\": 1";
        huge.replace(huge.find(needle), needle.size(), "\"min_free_bytes\": 9223372036854775807");
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(huge), one, std::nullopt);
        CHECK(!preflight.start_allowed);
        CHECK(!preflight.free_space_ok);
        CHECK(preflight.free_bytes_known);
    }

    {
        const xdcam::PreflightResult preflight = xdcam::RunPreflight(MustLoad(BaseConfig(dir, "")), four, std::nullopt);
        CHECK(preflight.start_allowed);
        CHECK(!preflight.four_channel_refused);
        CHECK_EQ(preflight.requested_channels, 4);
        CHECK_EQ(preflight.channel_count_source, "scan");
        CHECK_EQ(preflight.files_planned, 4);
        CHECK_EQ(preflight.assignments.size(), 4u);
        const std::string text = xdcam::FormatReport(four, run, &preflight);
        CHECK(ContainsLine(text, "mock: true"));
        CHECK(ContainsLine(text, "hardware_pass: false"));
        CHECK(ContainsLine(text, "record_started: false"));
        CHECK(text.find(kFourIndependentInputsRefusal) == std::string::npos);
    }

    {
        const std::filesystem::path bad = temp / "bad.json";
        std::ofstream output(bad);
        output << "{\n  \"notice\": \"not the required notice\",\n  \"devices\": []\n}\n";
        output.close();
        const xdcam::ScanResult scan = xdcam::MakeMockBackend(bad.string())->Scan();
        CHECK(!scan.error.empty());
    }

    {
        const std::filesystem::path decklink = std::filesystem::path(XDCAM_SOURCE_DIR) / "src" / "decklink_backend.cpp";
        const std::string source = ReadFile(decklink);
        CHECK(source.find("CreateDeckLinkIteratorInstance") != std::string::npos);
        CHECK(source.find("BMDDeckLinkPersistentID") != std::string::npos);
        CHECK(source.find("BMDDeckLinkVideoIOSupport") != std::string::npos);
        CHECK(source.find("BMDDeckLinkVideoInputConnections") != std::string::npos);
        CHECK(source.find("BMDDeckLinkVideoOutputConnections") != std::string::npos);
        CHECK(source.find("BMDDeckLinkAudioInputConnections") != std::string::npos);
        CHECK(source.find("BMDDeckLinkMaximumAudioChannels") != std::string::npos);
        CHECK(source.find("BMDDeckLinkDeviceInterface") != std::string::npos);
        CHECK(source.find("BMDDeckLinkProfileID") != std::string::npos);
        CHECK(source.find("BMDDeckLinkDuplex") != std::string::npos);
        CHECK(source.find("bmdDeckLinkStatusBusy") != std::string::npos);
        CHECK(source.find("GetDisplayModeIterator") != std::string::npos);
        CHECK(source.find("DoesSupportVideoMode") != std::string::npos);
        CHECK(source.find("EnableVideoInput") != std::string::npos);
        CHECK(source.find("EnableAudioInput") != std::string::npos);
        CHECK(source.find("bmdAudioSampleRate48kHz") != std::string::npos);
        CHECK(source.find("IID_IDeckLinkProfileAttributes") != std::string::npos);
        CHECK(source.find("IID_IDeckLinkProfileManager") != std::string::npos);
        CHECK(source.find("StartStreams") == std::string::npos);
        CHECK(source.find("avformat") == std::string::npos);
        CHECK(source.find("AVCodecContext") == std::string::npos);
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(XDCAM_SOURCE_DIR) / "src")) {
            if (entry.path().extension() != ".cpp" && entry.path().extension() != ".hpp") {
                continue;
            }
            const std::string text = ReadFile(entry.path());
            CHECK(text.find("avformat_write_header") == std::string::npos);
            CHECK(text.find("AVFormatContext") == std::string::npos);
        }
    }

    std::filesystem::remove_all(temp);
    if (g_failed != 0) {
        std::cerr << g_failed << " failure(s)\n";
        return 1;
    }
    std::cout << "phase0 tests passed\n";
    return 0;
}
