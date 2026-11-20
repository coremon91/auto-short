#include "config.hpp"

#include "json.hpp"

#include <fstream>
#include <set>
#include <sstream>

namespace xdcam {
namespace {

std::string UnknownKeys(const Value& object, std::initializer_list<const char*> allowed) {
    if (object.type != Value::Type::Object) {
        return "expected an object";
    }
    for (const auto& entry : object.o) {
        bool known = false;
        for (const char* key : allowed) {
            if (entry.first == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            return "unsupported key: " + entry.first;
        }
    }
    return {};
}

std::string RequireObject(const Value& parent, const char* key, const Value*& out) {
    const Value* value = parent.Find(key);
    if (value == nullptr) {
        return std::string("missing required key: ") + key;
    }
    if (value->type != Value::Type::Object) {
        return std::string("unsupported value for ") + key + ": expected an object";
    }
    out = value;
    return {};
}

std::string RequireInt(const Value& parent, const char* key, int64_t& out) {
    const Value* value = parent.Find(key);
    if (value == nullptr) {
        return std::string("missing required key: ") + key;
    }
    if (value->type != Value::Type::Int) {
        return std::string("unsupported value for ") + key + ": expected an integer";
    }
    out = value->i;
    return {};
}

std::string RequireString(const Value& parent, const char* key, std::string& out) {
    const Value* value = parent.Find(key);
    if (value == nullptr) {
        return std::string("missing required key: ") + key;
    }
    if (value->type != Value::Type::String) {
        return std::string("unsupported value for ") + key + ": expected a string";
    }
    out = value->s;
    return {};
}

std::string WithPrefix(std::string error, const char* prefix) {
    const std::string unsupported = "unsupported value for ";
    const std::string missing = "missing required key: ";
    if (error.compare(0, unsupported.size(), unsupported) == 0) {
        return unsupported + prefix + error.substr(unsupported.size());
    }
    if (error.compare(0, missing.size(), missing) == 0) {
        return missing + prefix + error.substr(missing.size());
    }
    return error;
}

std::string RequireExactInt(const Value& parent, const char* key, int64_t expected) {
    int64_t actual = 0;
    const std::string error = RequireInt(parent, key, actual);
    if (!error.empty()) {
        return error;
    }
    if (actual != expected) {
        return std::string("unsupported value for ") + key + ": " + std::to_string(actual);
    }
    return {};
}

std::string RequireExactString(const Value& parent, const char* key, const char* expected) {
    std::string actual;
    const std::string error = RequireString(parent, key, actual);
    if (!error.empty()) {
        return error;
    }
    if (actual != expected) {
        return std::string("unsupported value for ") + key + ": " + actual;
    }
    return {};
}

bool ValidChannelId(const std::string& id) {
    return id == "CH1" || id == "CH2" || id == "CH3" || id == "CH4";
}

std::string ReadFile(const std::string& path, std::string& text) {
    std::ifstream input(path);
    if (!input) {
        return "cannot read config: " + path;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
    return {};
}

}  // namespace

bool LoadConfigString(const std::string& text, Config& config, std::string& error) {
    config = Config{};
    Value root;
    try {
        root = ParseJson(text);
    } catch (const JsonError& ex) {
        error = std::string("JSON: ") + ex.what();
        return false;
    }
    if (root.type != Value::Type::Object) {
        error = "config root must be an object";
        return false;
    }
    error = UnknownKeys(root, {"backend", "channel_count", "channels", "output_dir", "min_free_bytes",
                               "require_audio_8ch", "on_signal_loss", "video", "audio", "fixture"});
    if (!error.empty()) {
        return false;
    }

    if (const Value* backend = root.Find("backend")) {
        if (backend->type != Value::Type::String ||
            (backend->s != "mock" && backend->s != "decklink" && backend->s != "auto")) {
            error = "unsupported value for backend";
            return false;
        }
        config.backend = backend->s;
    }

    if (const Value* count = root.Find("channel_count")) {
        if (count->type == Value::Type::Null) {
            config.channel_count.reset();
        } else if (count->type == Value::Type::Int && count->i >= 1 && count->i <= 4) {
            config.channel_count = static_cast<int>(count->i);
        } else {
            error = "unsupported value for channel_count";
            return false;
        }
    }

    if (const Value* channels = root.Find("channels")) {
        if (channels->type != Value::Type::Array || channels->a.empty() || channels->a.size() > 4) {
            error = "unsupported value for channels";
            return false;
        }
        std::set<std::string> ids;
        for (const Value& item : channels->a) {
            if (item.type != Value::Type::Object) {
                error = "unsupported value for channels";
                return false;
            }
            error = UnknownKeys(item, {"id", "persistent_id", "subdevice_index"});
            if (!error.empty()) {
                return false;
            }
            ChannelConfig channel;
            error = RequireString(item, "id", channel.id);
            if (!error.empty()) {
                return false;
            }
            if (!ValidChannelId(channel.id) || !ids.insert(channel.id).second) {
                error = "unsupported value for channels.id: " + channel.id;
                return false;
            }
            error = RequireString(item, "persistent_id", channel.persistent_id);
            if (!error.empty() || channel.persistent_id.empty()) {
                error = "unsupported value for channels.persistent_id";
                return false;
            }
            int64_t subdevice = 0;
            error = RequireInt(item, "subdevice_index", subdevice);
            if (!error.empty() || subdevice < 0 || subdevice > 64) {
                error = "unsupported value for channels.subdevice_index";
                return false;
            }
            channel.subdevice_index = static_cast<int>(subdevice);
            config.channels.push_back(std::move(channel));
        }
        config.has_channels = true;
        if (config.channel_count.has_value() &&
            *config.channel_count != static_cast<int>(config.channels.size())) {
            error = "channel_count does not match channels length";
            return false;
        }
    }

    error = RequireString(root, "output_dir", config.output_dir);
    if (!error.empty() || config.output_dir.empty()) {
        error = "unsupported value for output_dir";
        return false;
    }

    error = RequireInt(root, "min_free_bytes", config.min_free_bytes);
    if (!error.empty() || config.min_free_bytes < 0) {
        error = "unsupported value for min_free_bytes";
        return false;
    }

    if (const Value* require_audio = root.Find("require_audio_8ch")) {
        if (require_audio->type != Value::Type::Bool || !require_audio->b) {
            error = "unsupported value for require_audio_8ch";
            return false;
        }
    }

    error = RequireString(root, "on_signal_loss", config.on_signal_loss);
    if (!error.empty()) {
        return false;
    }
    if (config.on_signal_loss != "STOP_WITH_ERROR" && config.on_signal_loss != "KEEP_RECORDING_WITH_FLAG") {
        error = "unsupported value for on_signal_loss: " + config.on_signal_loss;
        return false;
    }

    const Value* video = nullptr;
    error = RequireObject(root, "video", video);
    if (!error.empty()) {
        return false;
    }
    error = UnknownKeys(*video, {"width", "height", "frame_rate_num", "frame_rate_den", "scan", "field_order",
                                 "pixel_format", "bitrate_bps"});
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*video, "width", 1920), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*video, "height", 1080), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*video, "frame_rate_num", 30000), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*video, "frame_rate_den", 1001), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactString(*video, "scan", "interlaced"), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactString(*video, "field_order", "top_field_first"), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactString(*video, "pixel_format", "yuv422p"), "video.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*video, "bitrate_bps", 50000000), "video.");
    if (!error.empty()) {
        return false;
    }

    const Value* audio = nullptr;
    error = RequireObject(root, "audio", audio);
    if (!error.empty()) {
        return false;
    }
    error = UnknownKeys(*audio, {"sample_rate", "channels_per_input", "format"});
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*audio, "sample_rate", 48000), "audio.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactInt(*audio, "channels_per_input", 8), "audio.");
    if (!error.empty()) {
        return false;
    }
    error = WithPrefix(RequireExactString(*audio, "format", "pcm_s24"), "audio.");
    if (!error.empty()) {
        return false;
    }

    if (const Value* fixture = root.Find("fixture")) {
        if (fixture->type != Value::Type::String || fixture->s.empty()) {
            error = "unsupported value for fixture";
            return false;
        }
        if (config.backend != "mock") {
            error = "fixture is only valid when backend is mock";
            return false;
        }
        config.has_fixture = true;
        config.fixture = fixture->s;
    }

    error.clear();
    return true;
}

bool LoadConfigFile(const std::string& path, Config& config, std::string& error) {
    std::string text;
    error = ReadFile(path, text);
    if (!error.empty()) {
        return false;
    }
    return LoadConfigString(text, config, error);
}

}  // namespace xdcam
