#include "decklink_backend.hpp"

#include "build_config.hpp"

#if !XDCAM_HAVE_DECKLINK
#error "decklink_backend.cpp is compiled only when DeckLink SDK headers are present"
#endif

#if !defined(__linux__)
#error "Phase 0 DeckLink backend uses the Linux SDK string ABI (free the returned strings)"
#endif

#include "DeckLinkAPI.h"
#include "DeckLinkAPIVersion.h"

#if BLACKMAGIC_DECKLINK_API_VERSION < 0x0b000000
#error "XDCAM DeckLink backend requires DeckLink SDK 11 or newer"
#endif

#include "util.hpp"

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xdcam {
namespace {

template <typename T>
struct ComPtr {
    T* pointer = nullptr;

    ComPtr() = default;
    explicit ComPtr(T* raw) : pointer(raw) {}
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : pointer(other.pointer) { other.pointer = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            pointer = other.pointer;
            other.pointer = nullptr;
        }
        return *this;
    }

    explicit operator bool() const { return pointer != nullptr; }
    T* operator->() const { return pointer; }
    T* Get() const { return pointer; }
    T** Put() {
        Reset();
        return &pointer;
    }
    void Reset() {
        if (pointer != nullptr) {
            pointer->Release();
            pointer = nullptr;
        }
    }
};

struct InputSession {
    ComPtr<IDeckLinkInput> input;
    bool video_enabled = false;
    bool audio_enabled = false;

    InputSession() = default;
    InputSession(const InputSession&) = delete;
    InputSession& operator=(const InputSession&) = delete;
    InputSession(InputSession&& other) noexcept
        : input(std::move(other.input)), video_enabled(other.video_enabled), audio_enabled(other.audio_enabled) {
        other.video_enabled = false;
        other.audio_enabled = false;
    }
    InputSession& operator=(InputSession&&) = delete;
    ~InputSession() {
        if (!input) {
            return;
        }
        if (audio_enabled) {
            input->DisableAudioInput();
        }
        if (video_enabled) {
            input->DisableVideoInput();
        }
    }
};

struct PixelProbe {
    BMDPixelFormat format;
    const char* name;
};

const PixelProbe kPixelFormats[] = {
    {bmdFormat8BitYUV, "8BitYUV"},
    {bmdFormat10BitYUV, "10BitYUV"},
    {bmdFormat8BitARGB, "8BitARGB"},
    {bmdFormat8BitBGRA, "8BitBGRA"},
    {bmdFormat10BitRGB, "10BitRGB"},
};

struct OpenChoice {
    bool usable = false;
    BMDDisplayMode mode = bmdModeUnknown;
    BMDPixelFormat pixel = bmdFormat8BitYUV;
    const char* pixel_name = "8BitYUV";
};

std::string FourCC(uint32_t value) {
    const char bytes[4] = {
        static_cast<char>((value >> 24) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>(value & 0xFF),
    };
    std::string text = "fourcc:";
    for (char byte : bytes) {
        if (byte < 32 || byte > 126) {
            std::ostringstream hex;
            hex << "0x" << std::hex << value;
            return hex.str();
        }
        text.push_back(byte);
    }
    return text;
}

std::string HResultText(HRESULT hr) {
    if (hr == S_OK) {
        return "S_OK";
    }
    if (hr == S_FALSE) {
        return "S_FALSE";
    }
    if (hr == E_ACCESSDENIED) {
        return "E_ACCESSDENIED";
    }
    if (hr == E_INVALIDARG) {
        return "E_INVALIDARG";
    }
    if (hr == E_NOINTERFACE) {
        return "E_NOINTERFACE";
    }
    if (hr == E_OUTOFMEMORY) {
        return "E_OUTOFMEMORY";
    }
    if (hr == E_FAIL) {
        return "E_FAIL";
    }
    std::ostringstream hex;
    hex << "0x" << std::hex << static_cast<uint32_t>(hr);
    return hex.str();
}

std::string TakeSdkString(const char* text) {
    if (text == nullptr) {
        return {};
    }
    std::string copy(text);
    std::free(const_cast<char*>(text));
    return copy;
}

std::string ProfileName(int64_t id) {
    switch (static_cast<BMDProfileID>(id)) {
        case bmdProfileOneSubDeviceFullDuplex:
            return "one_subdevice_full_duplex";
        case bmdProfileOneSubDeviceHalfDuplex:
            return "one_subdevice_half_duplex";
        case bmdProfileTwoSubDevicesFullDuplex:
            return "two_subdevices_full_duplex";
        case bmdProfileTwoSubDevicesHalfDuplex:
            return "two_subdevices_half_duplex";
        case bmdProfileFourSubDevicesHalfDuplex:
            return "four_subdevices_half_duplex";
        default:
            break;
    }
    return FourCC(static_cast<uint32_t>(id));
}

std::string DuplexName(int64_t id) {
    switch (static_cast<BMDDuplexMode>(id)) {
        case bmdDuplexFull:
            return "full";
        case bmdDuplexHalf:
            return "half";
        case bmdDuplexSimplex:
            return "simplex";
        case bmdDuplexInactive:
            return "inactive";
        default:
            break;
    }
    return FourCC(static_cast<uint32_t>(id));
}

std::string InterfaceName(int64_t id) {
    switch (static_cast<BMDDeviceInterface>(id)) {
        case bmdDeviceInterfacePCI:
            return "pci";
        case bmdDeviceInterfaceUSB:
            return "usb";
        case bmdDeviceInterfaceThunderbolt:
            return "thunderbolt";
        default:
            break;
    }
    return FourCC(static_cast<uint32_t>(id));
}

std::string FieldName(BMDFieldDominance dominance) {
    switch (dominance) {
        case bmdUpperFieldFirst:
            return "upper_field_first";
        case bmdLowerFieldFirst:
            return "lower_field_first";
        case bmdProgressiveFrame:
            return "progressive";
        case bmdProgressiveSegmentedFrame:
            return "progressive_segmented";
        case bmdUnknownFieldDominance:
            return "unknown";
        default:
            break;
    }
    return "unrecognized";
}

std::vector<std::string> DecodeMask(int64_t mask, const int64_t* bits, const char* const* names, std::size_t count) {
    std::vector<std::string> decoded;
    int64_t known = 0;
    for (std::size_t i = 0; i < count; ++i) {
        known |= bits[i];
        if ((mask & bits[i]) != 0) {
            decoded.emplace_back(names[i]);
        }
    }
    const int64_t unknown = mask & ~known;
    if (unknown != 0) {
        std::ostringstream hex;
        hex << "unknown_0x" << std::hex << static_cast<uint64_t>(unknown);
        decoded.push_back(hex.str());
    }
    return decoded;
}

std::vector<std::string> DecodeVideoConnections(int64_t mask) {
    const int64_t bits[] = {bmdVideoConnectionSDI,  bmdVideoConnectionHDMI,      bmdVideoConnectionOpticalSDI,
                            bmdVideoConnectionComponent, bmdVideoConnectionComposite, bmdVideoConnectionSVideo};
    const char* names[] = {"sdi", "hdmi", "optical_sdi", "component", "composite", "s_video"};
    return DecodeMask(mask, bits, names, 6);
}

std::vector<std::string> DecodeAudioConnections(int64_t mask) {
    const int64_t bits[] = {bmdAudioConnectionEmbedded, bmdAudioConnectionAESEBU,    bmdAudioConnectionAnalog,
                            bmdAudioConnectionAnalogXLR, bmdAudioConnectionAnalogRCA, bmdAudioConnectionMicrophone};
    const char* names[] = {"embedded", "aes_ebu", "analog", "analog_xlr", "analog_rca", "microphone"};
    return DecodeMask(mask, bits, names, 6);
}

bool SupportsFormat(IDeckLinkInput* input, BMDDisplayMode mode, BMDPixelFormat format) {
#if BLACKMAGIC_DECKLINK_API_VERSION >= 0x0b050000
    bool supported = false;
    BMDDisplayMode actual = mode;
    const HRESULT hr = input->DoesSupportVideoMode(bmdVideoConnectionUnspecified, mode, format, bmdNoVideoInputConversion,
                                                    bmdSupportedVideoModeDefault, &actual, &supported);
    return hr == S_OK && supported;
#else
    bool supported = false;
    const HRESULT hr = input->DoesSupportVideoMode(static_cast<BMDVideoConnection>(0), mode, format,
                                                    bmdSupportedVideoModeDefault, &supported);
    return hr == S_OK && supported;
#endif
}

void ReadAttributes(IDeckLink* device, DeviceInfo& info, bool& capture_busy) {
    ComPtr<IDeckLinkProfileAttributes> attributes;
    if (device->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(attributes.Put())) != S_OK) {
        attributes.Reset();
    }
    if (attributes) {
        int64_t value = 0;
        if (attributes->GetInt(BMDDeckLinkPersistentID, &value) == S_OK) {
            info.persistent_id_available = true;
            info.persistent_id = std::to_string(static_cast<uint64_t>(value));
        }
        if (attributes->GetInt(BMDDeckLinkDeviceInterface, &value) == S_OK) {
            info.device_interface = InterfaceName(value);
        }
        if (attributes->GetInt(BMDDeckLinkDeviceGroupID, &value) == S_OK) {
            info.device_group_id = std::to_string(static_cast<uint64_t>(value));
        }
        if (attributes->GetInt(BMDDeckLinkVideoIOSupport, &value) == S_OK) {
            if ((value & bmdDeviceSupportsCapture) != 0) {
                info.video_io.emplace_back("capture");
            }
            if ((value & bmdDeviceSupportsPlayback) != 0) {
                info.video_io.emplace_back("playback");
            }
        }
        if (attributes->GetInt(BMDDeckLinkVideoInputConnections, &value) == S_OK) {
            info.input_connectors = DecodeVideoConnections(value);
        }
        if (attributes->GetInt(BMDDeckLinkVideoOutputConnections, &value) == S_OK) {
            info.output_connectors = DecodeVideoConnections(value);
        }
        if (attributes->GetInt(BMDDeckLinkAudioInputConnections, &value) == S_OK) {
            info.audio_input = DecodeAudioConnections(value);
        }
        if (attributes->GetInt(BMDDeckLinkMaximumAudioChannels, &value) == S_OK) {
            info.max_audio_channels = value;
            info.audio_8ch_supported = value >= 8;
        }
        if (attributes->GetInt(BMDDeckLinkSubDeviceIndex, &value) == S_OK) {
            info.subdevice_index = static_cast<int>(value);
        }
        if (attributes->GetInt(BMDDeckLinkNumberOfSubDevices, &value) == S_OK) {
            info.number_of_subdevices = static_cast<int>(value);
        }
        if (attributes->GetInt(BMDDeckLinkProfileID, &value) == S_OK) {
            info.active_profile = ProfileName(value);
        }
        if (attributes->GetInt(BMDDeckLinkDuplex, &value) == S_OK) {
            info.duplex = DuplexName(value);
        }
        const char* model = nullptr;
        if (attributes->GetString(BMDDeckLinkModelName, &model) == S_OK) {
            info.model_name = TakeSdkString(model);
        }
    }

    ComPtr<IDeckLinkStatus> status;
    if (device->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void**>(status.Put())) == S_OK && status) {
        int64_t busy = 0;
        if (status->GetInt(bmdDeckLinkStatusBusy, &busy) == S_OK) {
            // Any busy bit is reported. Only capture busy skips the open probe;
            // playback on a full-duplex card can leave capture openable.
            info.in_use = busy != 0;
            std::vector<std::string> parts;
            if ((busy & bmdDeviceCaptureBusy) != 0) {
                parts.emplace_back("capture");
                capture_busy = true;
            }
            if ((busy & bmdDevicePlaybackBusy) != 0) {
                parts.emplace_back("playback");
            }
            if ((busy & bmdDeviceSerialPortBusy) != 0) {
                parts.emplace_back("serial");
            }
            if (!parts.empty()) {
                info.busy_detail = Join(parts, ",");
            }
        }
    }
}

void ReadProfiles(IDeckLink* device, DeviceInfo& info) {
    ComPtr<IDeckLinkProfileManager> manager;
    if (device->QueryInterface(IID_IDeckLinkProfileManager, reinterpret_cast<void**>(manager.Put())) != S_OK || !manager) {
        if (info.active_profile.empty()) {
            info.active_profile = "single_profile";
        }
        info.profile_list = info.active_profile;
        return;
    }
    ComPtr<IDeckLinkProfileIterator> iterator;
    if (manager->GetProfiles(iterator.Put()) != S_OK || !iterator) {
        info.profile_list = info.active_profile.empty() ? "unavailable" : info.active_profile;
        return;
    }
    std::vector<std::string> names;
    std::string active;
    IDeckLinkProfile* raw = nullptr;
    while (iterator->Next(&raw) == S_OK && raw != nullptr) {
        ComPtr<IDeckLinkProfile> profile(raw);
        bool is_active = false;
        profile->IsActive(&is_active);
        ComPtr<IDeckLinkProfileAttributes> attributes;
        std::string name = "unavailable";
        if (profile->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(attributes.Put())) == S_OK &&
            attributes) {
            int64_t id = 0;
            if (attributes->GetInt(BMDDeckLinkProfileID, &id) == S_OK) {
                name = ProfileName(id);
            }
        }
        if (is_active) {
            active = name;
            names.push_back(name + "(active)");
        } else {
            names.push_back(name);
        }
    }
    if (info.active_profile.empty() && !active.empty()) {
        info.active_profile = active;
    }
    info.profile_list = names.empty() ? info.active_profile : Join(names, ",");
    if (info.profile_list.empty()) {
        info.profile_list = "unavailable";
    }
}

void ReadActiveConnector(IDeckLink* device, DeviceInfo& info) {
    ComPtr<IDeckLinkConfiguration> configuration;
    if (device->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(configuration.Put())) != S_OK ||
        !configuration) {
        return;
    }
    int64_t connection = 0;
    if (configuration->GetInt(bmdDeckLinkConfigVideoInputConnection, &connection) != S_OK) {
        return;
    }
    if (connection == 0) {
        info.active_input_connector = "unspecified";
        return;
    }
    const std::vector<std::string> names = DecodeVideoConnections(connection);
    info.active_input_connector = names.empty() ? "unspecified" : Join(names, ",");
}

bool ListModes(IDeckLinkInput* input, DeviceInfo& info, OpenChoice& choice) {
    ComPtr<IDeckLinkDisplayModeIterator> iterator;
    if (input->GetDisplayModeIterator(iterator.Put()) != S_OK || !iterator) {
        return false;
    }
    struct Listed {
        BMDDisplayMode id;
        bool yuv8 = false;
        bool yuv10 = false;
    };
    std::vector<Listed> listed;
    IDeckLinkDisplayMode* raw = nullptr;
    while (iterator->Next(&raw) == S_OK && raw != nullptr) {
        ComPtr<IDeckLinkDisplayMode> mode(raw);
        DisplayModeInfo described;
        const char* name = nullptr;
        if (mode->GetName(&name) == S_OK) {
            described.name = TakeSdkString(name);
        }
        described.width = static_cast<int>(mode->GetWidth());
        described.height = static_cast<int>(mode->GetHeight());
        BMDTimeValue duration = 0;
        BMDTimeScale scale = 0;
        if (mode->GetFrameRate(&duration, &scale) == S_OK && duration != 0) {
            described.frame_rate_num = static_cast<int64_t>(scale);
            described.frame_rate_den = static_cast<int64_t>(duration);
        }
        described.field_dominance = FieldName(mode->GetFieldDominance());
        Listed entry;
        entry.id = mode->GetDisplayMode();
        for (const PixelProbe& pixel : kPixelFormats) {
            if (!SupportsFormat(input, entry.id, pixel.format)) {
                continue;
            }
            described.pixel_formats.emplace_back(pixel.name);
            bool seen = false;
            for (const std::string& existing : info.pixel_formats) {
                if (existing == pixel.name) {
                    seen = true;
                }
            }
            if (!seen) {
                info.pixel_formats.emplace_back(pixel.name);
            }
            if (pixel.format == bmdFormat8BitYUV) {
                entry.yuv8 = true;
            }
            if (pixel.format == bmdFormat10BitYUV) {
                entry.yuv10 = true;
            }
        }
        listed.push_back(entry);
        info.display_modes.push_back(std::move(described));
    }
    const Listed* preferred = nullptr;
    for (const Listed& entry : listed) {
        if (entry.id == bmdModeHD1080i5994 && (entry.yuv8 || entry.yuv10)) {
            preferred = &entry;
            break;
        }
    }
    if (preferred == nullptr) {
        for (const Listed& entry : listed) {
            if (entry.yuv8 || entry.yuv10) {
                preferred = &entry;
                break;
            }
        }
    }
    if (preferred == nullptr) {
        return false;
    }
    choice.usable = true;
    choice.mode = preferred->id;
    if (preferred->yuv8) {
        choice.pixel = bmdFormat8BitYUV;
        choice.pixel_name = "8BitYUV";
    } else {
        choice.pixel = bmdFormat10BitYUV;
        choice.pixel_name = "10BitYUV";
    }
    return true;
}

void Describe(IDeckLink* device, DeviceInfo& info, OpenChoice& choice, bool& capture_busy) {
    info.active_input_connector = "unavailable";
    const char* display = nullptr;
    if (device->GetDisplayName(&display) == S_OK) {
        info.display_name = TakeSdkString(display);
    }
    if (info.display_name.empty()) {
        info.display_name = "unavailable";
    }
    const char* model = nullptr;
    if (device->GetModelName(&model) == S_OK) {
        info.model_name = TakeSdkString(model);
    }
    ReadAttributes(device, info, capture_busy);
    ReadProfiles(device, info);
    ReadActiveConnector(device, info);
    bool capture = false;
    for (const std::string& io : info.video_io) {
        if (io == "capture") {
            capture = true;
        }
    }
    if (!capture) {
        return;
    }
    InputInstanceInfo instance;
    instance.subdevice_index = info.subdevice_index;
    instance.simultaneous_capture_openable = false;
    instance.streams_started = false;
    ComPtr<IDeckLinkInput> input;
    if (device->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(input.Put())) != S_OK || !input) {
        instance.open_probe = "IDeckLinkInput unavailable";
        info.inputs.push_back(instance);
        return;
    }
    const bool have_mode = ListModes(input.Get(), info, choice);
    if (capture_busy) {
        instance.open_probe = "not_opened_capture_busy";
    } else if (!have_mode) {
        instance.open_probe = "no_supported_mode";
    } else {
        instance.open_probe = "pending_open";
    }
    info.inputs.push_back(instance);
}

void ProbeOpens(const std::vector<ComPtr<IDeckLink>>& devices, std::vector<DeviceInfo>& infos,
                const std::vector<OpenChoice>& choices, const std::vector<bool>& capture_busy) {
    std::vector<InputSession> held;
    held.reserve(devices.size());
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (infos[i].inputs.empty() || capture_busy[i] || !choices[i].usable) {
            continue;
        }
        InputSession session;
        if (devices[i]->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(session.input.Put())) != S_OK ||
            !session.input) {
            infos[i].inputs[0].open_probe = "IDeckLinkInput unavailable";
            continue;
        }
        const HRESULT video = session.input->EnableVideoInput(choices[i].mode, choices[i].pixel, bmdVideoInputFlagDefault);
        if (video != S_OK) {
            infos[i].inputs[0].simultaneous_capture_openable = false;
            infos[i].inputs[0].open_probe = "EnableVideoInput " + HResultText(video);
            if (video == E_ACCESSDENIED) {
                infos[i].in_use = true;
                if (infos[i].busy_detail.empty()) {
                    infos[i].busy_detail = "capture";
                }
            }
            continue;
        }
        session.video_enabled = true;
        HRESULT audio = session.input->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger, 8);
        std::string audio_note = "audio_32bit_8ch_ok";
        if (audio != S_OK) {
            audio = session.input->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, 8);
            audio_note = "audio_16bit_8ch_ok";
        }
        if (audio == S_OK) {
            session.audio_enabled = true;
            infos[i].audio_8ch_openable = true;
        } else {
            infos[i].audio_8ch_openable = false;
            audio_note = "audio_8ch_" + HResultText(audio);
        }
        infos[i].inputs[0].simultaneous_capture_openable = true;
        infos[i].inputs[0].open_probe =
            std::string("EnableVideoInput ") + choices[i].pixel_name + " " + audio_note + " without_stream_start";
        held.push_back(std::move(session));
    }
}

class DeckLinkBackend : public DeviceBackend {
   public:
    ScanResult Scan() override {
        ScanResult result;
        result.backend = "decklink";
        result.mock = false;
        result.decklink_header_version = BLACKMAGIC_DECKLINK_API_VERSION_STRING;
        ComPtr<IDeckLinkAPIInformation> api(CreateDeckLinkAPIInformationInstance());
        if (api) {
            int64_t version = 0;
            if (api->GetInt(BMDDeckLinkAPIVersion, &version) == S_OK) {
                std::ostringstream hex;
                hex << "0x" << std::hex << static_cast<uint64_t>(version);
                result.decklink_runtime_version = hex.str();
                if (version < static_cast<int64_t>(BLACKMAGIC_DECKLINK_API_VERSION)) {
                    result.decklink_driver_note =
                        "runtime API version is older than the SDK headers used to compile this binary";
                }
            }
        }
        ComPtr<IDeckLinkIterator> iterator(CreateDeckLinkIteratorInstance());
        if (!iterator) {
            result.error =
                "CreateDeckLinkIteratorInstance returned null. The Desktop Video driver is not loaded. "
                "No DeckLink devices were enumerated. This is not a hardware pass.";
            return result;
        }
        std::vector<ComPtr<IDeckLink>> devices;
        std::vector<OpenChoice> choices;
        std::vector<bool> capture_busy;
        IDeckLink* raw = nullptr;
        while (iterator->Next(&raw) == S_OK && raw != nullptr) {
            devices.emplace_back(raw);
            DeviceInfo info;
            OpenChoice choice;
            bool busy = false;
            Describe(devices.back().Get(), info, choice, busy);
            result.devices.push_back(std::move(info));
            choices.push_back(choice);
            capture_busy.push_back(busy);
        }
        ProbeOpens(devices, result.devices, choices, capture_busy);
        result.independent_inputs = CountIndependentInputs(result.devices);
        return result;
    }
};

}  // namespace

std::unique_ptr<DeviceBackend> MakeDeckLinkBackend() { return std::make_unique<DeckLinkBackend>(); }

}  // namespace xdcam
