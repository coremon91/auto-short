#include "mock_backend.hpp"

#include "fixture_data.hpp"
#include "json.hpp"

#include <fstream>
#include <set>
#include <sstream>

namespace xdcam {
namespace {

std::string ParseFixture(const std::string& text, ScanResult& result);

class MockBackend : public DeviceBackend {
   public:
    explicit MockBackend(std::string fixture) : fixture_(std::move(fixture)) {}

    ScanResult Scan() override {
        ScanResult result;
        result.backend = "mock";
        result.mock = true;
        result.fixture = fixture_;
        std::string text;
        std::string error = LoadFixtureText(fixture_, text);
        if (!error.empty()) {
            result.error = error;
            return result;
        }
        error = ParseFixture(text, result);
        if (!error.empty()) {
            result.devices.clear();
            result.independent_inputs = 0;
            result.notice.clear();
            result.error = error;
            return result;
        }
        result.independent_inputs = CountIndependentInputs(result.devices);
        return result;
    }

   private:
    std::string fixture_;

    static bool LooksLikePath(const std::string& name) {
        return name.find('/') != std::string::npos || (name.size() >= 5 && name.compare(name.size() - 5, 5, ".json") == 0);
    }

    static std::string LoadFixtureText(const std::string& name, std::string& text) {
        if (!LooksLikePath(name)) {
            const char* built_in = nullptr;
            if (name == "one-input") {
                built_in = kFixtureOneInput;
            } else if (name == "two-input") {
                built_in = kFixtureTwoInput;
            } else if (name == "four-input") {
                built_in = kFixtureFourInput;
            } else if (name == "busy") {
                built_in = kFixtureBusy;
            } else if (name == "playback-busy") {
                built_in = kFixturePlaybackBusy;
            } else if (name == "no-audio-8ch") {
                built_in = kFixtureNoAudio8ch;
            }
            if (built_in == nullptr) {
                return "unknown mock fixture: " + name;
            }
            text = built_in;
            return {};
        }
        std::ifstream input(name);
        if (!input) {
            return "cannot read mock fixture: " + name;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        text = buffer.str();
        return {};
    }
};

std::string UnknownKeys(const Value& object, std::initializer_list<const char*> allowed) {
    for (const auto& entry : object.o) {
        bool known = false;
        for (const char* key : allowed) {
            if (entry.first == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            return "fixture: unsupported key: " + entry.first;
        }
    }
    return {};
}

std::string NeedString(const Value& object, const char* key, std::string& out) {
    const Value* value = object.Find(key);
    if (value == nullptr || value->type != Value::Type::String || value->s.empty()) {
        return std::string("fixture: unsupported value for ") + key;
    }
    out = value->s;
    return {};
}

std::string NeedInt(const Value& object, const char* key, int64_t& out) {
    const Value* value = object.Find(key);
    if (value == nullptr || value->type != Value::Type::Int) {
        return std::string("fixture: unsupported value for ") + key;
    }
    out = value->i;
    return {};
}

std::string NeedBool(const Value& object, const char* key, bool& out) {
    const Value* value = object.Find(key);
    if (value == nullptr || value->type != Value::Type::Bool) {
        return std::string("fixture: unsupported value for ") + key;
    }
    out = value->b;
    return {};
}

bool BusyDetailHas(const std::string& detail, const char* token) {
    std::string::size_type start = 0;
    while (start <= detail.size()) {
        const std::string::size_type comma = detail.find(',', start);
        const std::string part = detail.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (part == token) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

std::string NeedStringList(const Value& object, const char* key, std::vector<std::string>& out) {
    const Value* value = object.Find(key);
    if (value == nullptr || value->type != Value::Type::Array) {
        return std::string("fixture: unsupported value for ") + key;
    }
    for (const Value& item : value->a) {
        if (item.type != Value::Type::String || item.s.empty()) {
            return std::string("fixture: unsupported value for ") + key;
        }
        out.push_back(item.s);
    }
    return {};
}

std::string ParseFixture(const std::string& text, ScanResult& result) {
    Value root;
    try {
        root = ParseJson(text);
    } catch (const JsonError& ex) {
        return std::string("fixture: JSON: ") + ex.what();
    }
    if (root.type != Value::Type::Object) {
        return "fixture: root must be an object";
    }
    std::string error = UnknownKeys(root, {"notice", "devices"});
    if (!error.empty()) {
        return error;
    }
    error = NeedString(root, "notice", result.notice);
    if (!error.empty()) {
        return error;
    }
    if (result.notice != kMockNotice) {
        return "fixture: notice must state that this is a mock and not a hardware pass";
    }
    const Value* devices = root.Find("devices");
    if (devices == nullptr || devices->type != Value::Type::Array || devices->a.empty()) {
        return "fixture: devices must be a non-empty array";
    }
    std::set<std::string> keys;
    for (const Value& item : devices->a) {
        if (item.type != Value::Type::Object) {
            return "fixture: device must be an object";
        }
        error = UnknownKeys(item, {"display_name", "persistent_id", "model_name", "device_interface", "device_group_id",
                                    "video_io", "active_profile", "profile_list", "duplex", "in_use", "busy_detail",
                                    "input_connectors", "output_connectors", "active_input_connector", "audio_input",
                                    "max_audio_channels", "audio_8ch_supported", "audio_8ch_openable", "subdevice_index",
                                    "number_of_subdevices", "display_modes", "input_instances"});
        if (!error.empty()) {
            return error;
        }
        DeviceInfo device;
        error = NeedString(item, "display_name", device.display_name);
        if (!error.empty()) {
            return error;
        }
        if (device.display_name.find("4K Extreme") != std::string::npos) {
            return "fixture: display_name must not claim a DeckLink 4K Extreme result";
        }
        const Value* persistent = item.Find("persistent_id");
        if (persistent == nullptr ||
            (persistent->type != Value::Type::String && persistent->type != Value::Type::Null)) {
            return "fixture: unsupported value for persistent_id";
        }
        if (persistent->type == Value::Type::String) {
            if (persistent->s.empty()) {
                return "fixture: unsupported value for persistent_id";
            }
            device.persistent_id_available = true;
            device.persistent_id = persistent->s;
        }
        error = NeedString(item, "model_name", device.model_name);
        if (!error.empty()) {
            return error;
        }
        error = NeedString(item, "device_interface", device.device_interface);
        if (!error.empty()) {
            return error;
        }
        error = NeedString(item, "device_group_id", device.device_group_id);
        if (!error.empty()) {
            return error;
        }
        error = NeedStringList(item, "video_io", device.video_io);
        if (!error.empty() || device.video_io.empty()) {
            return "fixture: unsupported value for video_io";
        }
        for (const std::string& io : device.video_io) {
            if (io != "capture" && io != "playback") {
                return "fixture: unsupported value for video_io";
            }
        }
        error = NeedString(item, "active_profile", device.active_profile);
        if (!error.empty()) {
            return error;
        }
        error = NeedString(item, "profile_list", device.profile_list);
        if (!error.empty()) {
            return error;
        }
        error = NeedString(item, "duplex", device.duplex);
        if (!error.empty()) {
            return error;
        }
        error = NeedBool(item, "in_use", device.in_use);
        if (!error.empty()) {
            return error;
        }
        if (const Value* busy = item.Find("busy_detail")) {
            if (busy->type != Value::Type::String) {
                return "fixture: unsupported value for busy_detail";
            }
            device.busy_detail = busy->s;
        }
        if (device.in_use && device.busy_detail.empty()) {
            return "fixture: in_use requires busy_detail";
        }
        error = NeedStringList(item, "input_connectors", device.input_connectors);
        if (!error.empty()) {
            return error;
        }
        error = NeedStringList(item, "output_connectors", device.output_connectors);
        if (!error.empty()) {
            return error;
        }
        error = NeedString(item, "active_input_connector", device.active_input_connector);
        if (!error.empty()) {
            return error;
        }
        error = NeedStringList(item, "audio_input", device.audio_input);
        if (!error.empty()) {
            return error;
        }
        error = NeedInt(item, "max_audio_channels", device.max_audio_channels);
        if (!error.empty() || device.max_audio_channels < 0) {
            return "fixture: unsupported value for max_audio_channels";
        }
        error = NeedBool(item, "audio_8ch_supported", device.audio_8ch_supported);
        if (!error.empty()) {
            return error;
        }
        error = NeedBool(item, "audio_8ch_openable", device.audio_8ch_openable);
        if (!error.empty()) {
            return error;
        }
        if (device.audio_8ch_supported != (device.max_audio_channels >= 8)) {
            return "fixture: audio_8ch_supported does not match max_audio_channels";
        }
        if (device.audio_8ch_openable && !device.audio_8ch_supported) {
            return "fixture: audio_8ch_openable requires audio_8ch_supported";
        }
        int64_t subdevice = 0;
        error = NeedInt(item, "subdevice_index", subdevice);
        if (!error.empty() || subdevice < 0) {
            return "fixture: unsupported value for subdevice_index";
        }
        device.subdevice_index = static_cast<int>(subdevice);
        int64_t subdevice_count = 0;
        error = NeedInt(item, "number_of_subdevices", subdevice_count);
        if (!error.empty() || subdevice_count < 1) {
            return "fixture: unsupported value for number_of_subdevices";
        }
        device.number_of_subdevices = static_cast<int>(subdevice_count);
        const Value* modes = item.Find("display_modes");
        if (modes == nullptr || modes->type != Value::Type::Array || modes->a.empty()) {
            return "fixture: display_modes must be a non-empty array";
        }
        for (const Value& mode_value : modes->a) {
            if (mode_value.type != Value::Type::Object) {
                return "fixture: display mode must be an object";
            }
            error = UnknownKeys(mode_value, {"name", "width", "height", "frame_rate_num", "frame_rate_den",
                                             "field_dominance", "pixel_formats"});
            if (!error.empty()) {
                return error;
            }
            DisplayModeInfo mode;
            error = NeedString(mode_value, "name", mode.name);
            if (!error.empty()) {
                return error;
            }
            int64_t width = 0;
            int64_t height = 0;
            error = NeedInt(mode_value, "width", width);
            if (!error.empty() || width < 1) {
                return "fixture: unsupported value for width";
            }
            error = NeedInt(mode_value, "height", height);
            if (!error.empty() || height < 1) {
                return "fixture: unsupported value for height";
            }
            mode.width = static_cast<int>(width);
            mode.height = static_cast<int>(height);
            error = NeedInt(mode_value, "frame_rate_num", mode.frame_rate_num);
            if (!error.empty() || mode.frame_rate_num < 1) {
                return "fixture: unsupported value for frame_rate_num";
            }
            error = NeedInt(mode_value, "frame_rate_den", mode.frame_rate_den);
            if (!error.empty() || mode.frame_rate_den < 1) {
                return "fixture: unsupported value for frame_rate_den";
            }
            error = NeedString(mode_value, "field_dominance", mode.field_dominance);
            if (!error.empty()) {
                return error;
            }
            if (mode.field_dominance != "upper_field_first" && mode.field_dominance != "lower_field_first" &&
                mode.field_dominance != "progressive" && mode.field_dominance != "progressive_segmented" &&
                mode.field_dominance != "unknown") {
                return "fixture: unsupported value for field_dominance";
            }
            error = NeedStringList(mode_value, "pixel_formats", mode.pixel_formats);
            if (!error.empty() || mode.pixel_formats.empty()) {
                return "fixture: unsupported value for pixel_formats";
            }
            for (const std::string& pixel : mode.pixel_formats) {
                bool seen = false;
                for (const std::string& existing : device.pixel_formats) {
                    if (existing == pixel) {
                        seen = true;
                    }
                }
                if (!seen) {
                    device.pixel_formats.push_back(pixel);
                }
            }
            device.display_modes.push_back(std::move(mode));
        }
        const Value* inputs = item.Find("input_instances");
        if (inputs == nullptr || inputs->type != Value::Type::Array) {
            return "fixture: input_instances must be an array";
        }
        bool capture = false;
        for (const std::string& io : device.video_io) {
            if (io == "capture") {
                capture = true;
            }
        }
        if (capture && inputs->a.size() != 1) {
            return "fixture: a capture device has one input instance; connectors are not independent inputs";
        }
        if (!capture && !inputs->a.empty()) {
            return "fixture: input_instances require video_io capture";
        }
        for (const Value& input_value : inputs->a) {
            if (input_value.type != Value::Type::Object) {
                return "fixture: input instance must be an object";
            }
            error = UnknownKeys(input_value, {"subdevice_index", "simultaneous_capture_openable", "open_probe"});
            if (!error.empty()) {
                return error;
            }
            InputInstanceInfo input;
            int64_t input_sub = 0;
            error = NeedInt(input_value, "subdevice_index", input_sub);
            if (!error.empty()) {
                return error;
            }
            input.subdevice_index = static_cast<int>(input_sub);
            if (input.subdevice_index != device.subdevice_index) {
                return "fixture: input instance subdevice_index must match the device";
            }
            error = NeedBool(input_value, "simultaneous_capture_openable", input.simultaneous_capture_openable);
            if (!error.empty()) {
                return error;
            }
            error = NeedString(input_value, "open_probe", input.open_probe);
            if (!error.empty()) {
                return error;
            }
            input.streams_started = false;
            if (device.in_use && input.simultaneous_capture_openable && BusyDetailHas(device.busy_detail, "capture")) {
                return "fixture: a capture-busy device cannot be simultaneously openable";
            }
            if (input.simultaneous_capture_openable == false && device.audio_8ch_openable) {
                return "fixture: audio_8ch_openable requires an openable input";
            }
            device.inputs.push_back(std::move(input));
        }
        if (device.inputs.empty() && device.audio_8ch_openable) {
            return "fixture: audio_8ch_openable requires an openable input";
        }
        const std::string key = DeviceKey(device, device.subdevice_index);
        if (!keys.insert(key).second) {
            return "fixture: duplicate device identity";
        }
        result.devices.push_back(std::move(device));
    }
    return {};
}

}  // namespace

std::unique_ptr<DeviceBackend> MakeMockBackend(std::string fixture) {
    return std::make_unique<MockBackend>(std::move(fixture));
}

}  // namespace xdcam
