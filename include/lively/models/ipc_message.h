#pragma once
// Port of Lively.Models/Message/* — the JSON IPC contract between Lively and
// the wallpaper processes (players, web wallpapers, external programs).
//
// Wire-format rules (must match Newtonsoft.Json exactly — verified by golden tests):
//  * `Type` is serialized as an INTEGER (no StringEnumConverter on IpcMessage),
//    e.g. LivelySuspendCmd -> {"Type":7}. The C# comment in WebWebView2.cs
//    (`//"{\"Type\":7}"`) documents this.
//  * `Type` appears FIRST in the object (JsonProperty(Order = -2)); other
//    properties follow in declaration order (Newtonsoft default).
//  * null string properties are emitted as "Name": null (Newtonsoft default,
//    NullValueHandling.Include) — LivelyProperty files rely on this.
//  * Enum properties (e.g. LivelyMessageConsole.Category) also serialize as
//    integers by default.
//
// Reference C#: Lively.Models/Message/*.cs

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

// ordered_json preserves insertion order — Newtonsoft emits Type first
// (JsonProperty(Order = -2)) then properties in declaration order; the default
// nlohmann object map would alphabetize keys and break byte-equivalence.

namespace lively::models {

// --------------------------------- enums ---------------------------------

// Lively.Models/Message/MessageType.cs (ordinal == wire value)
enum class MessageType : int {
    msg_hwnd = 0,
    msg_console,
    msg_wploaded,
    msg_screenshot,
    cmd_reload,
    cmd_close,
    cmd_screenshot,
    cmd_suspend,
    cmd_resume,
    cmd_volume,
    lsp_perfcntr,
    lsp_nowplaying,
    lp_slider,
    lp_textbox,
    lp_dropdown,
    lp_fdropdown,
    lp_button,
    lp_cpicker,
    lp_chekbox,
    lp_dropdown_scaler,
};

// Lively.Models/Message/ConsoleMessageType.cs
enum class ConsoleMessageType : int {
    log = 0,
    error,
    console,
};

// Lively.Models/Message/ScreenshotFormat.cs
enum class ScreenshotFormat : int {
    jpeg = 0,
    png,
    webp,
    bmp,
};

// --------------------------------- IpcMessage ---------------------------------

// C# abstract IpcMessage: [JsonProperty(Order = -2)] MessageType Type { get; }
class IpcMessage {
public:
    explicit IpcMessage(MessageType type) : type_(type) {}
    virtual ~IpcMessage(); // out-of-line (defined in ipc_message.cpp)

    MessageType type() const noexcept { return type_; }

    // Newtonsoft-identical serialization. Derived classes extend this by
    // overriding write_members() (called AFTER the Type member, in declaration
    // order) — mirrors C# property ordering.
    virtual void to_json(nlohmann::ordered_json& j) const {
        j["Type"] = static_cast<int>(type_);
    }

private:
    MessageType type_;
};

// Macro keeps member order + names identical to the C# source.
#define LIVELY_IPC_JSON_IMPL(CLASS, BASE)                                                    \
    void to_json(nlohmann::ordered_json& j) const override {                                          \
        BASE::to_json(j);                                                                     \
        nlohmann::ordered_json members = nlohmann::ordered_json::object();                                     \
        write_members(members);                                                               \
        for (auto it = members.begin(); it != members.end(); ++it) j[it.key()] = it.value();  \
    }                                                                                         \
    void write_members(nlohmann::ordered_json& j) const

// ------------------------------ command messages ------------------------------

// LivelyCloseCmd: {}
class LivelyCloseCmd final : public IpcMessage {
public:
    LivelyCloseCmd() : IpcMessage(MessageType::cmd_close) {}
    LIVELY_IPC_JSON_IMPL(LivelyCloseCmd, IpcMessage) { (void)j; }
};

// LivelySuspendCmd: {}
class LivelySuspendCmd final : public IpcMessage {
public:
    LivelySuspendCmd() : IpcMessage(MessageType::cmd_suspend) {}
    LIVELY_IPC_JSON_IMPL(LivelySuspendCmd, IpcMessage) { (void)j; }
};

// LivelyResumeCmd: {}
class LivelyResumeCmd final : public IpcMessage {
public:
    LivelyResumeCmd() : IpcMessage(MessageType::cmd_resume) {}
    LIVELY_IPC_JSON_IMPL(LivelyResumeCmd, IpcMessage) { (void)j; }
};

// LivelyReloadCmd: {}
class LivelyReloadCmd final : public IpcMessage {
public:
    LivelyReloadCmd() : IpcMessage(MessageType::cmd_reload) {}
    LIVELY_IPC_JSON_IMPL(LivelyReloadCmd, IpcMessage) { (void)j; }
};

// public int Volume
class LivelyVolumeCmd final : public IpcMessage {
public:
    LivelyVolumeCmd() : IpcMessage(MessageType::cmd_volume) {}
    LIVELY_IPC_JSON_IMPL(LivelyVolumeCmd, IpcMessage) { j["Volume"] = volume; }
    int volume = 0;
};

// public long Hwnd
class LivelyMessageHwnd final : public IpcMessage {
public:
    LivelyMessageHwnd() : IpcMessage(MessageType::msg_hwnd) {}
    LIVELY_IPC_JSON_IMPL(LivelyMessageHwnd, IpcMessage) { j["Hwnd"] = hwnd; }
    std::int64_t hwnd = 0;
};

// public bool Success
class LivelyMessageWallpaperLoaded final : public IpcMessage {
public:
    LivelyMessageWallpaperLoaded() : IpcMessage(MessageType::msg_wploaded) {}
    LIVELY_IPC_JSON_IMPL(LivelyMessageWallpaperLoaded, IpcMessage) { j["Success"] = success; }
    bool success = false;
};

// public string FileName; public bool Success
class LivelyMessageScreenshot final : public IpcMessage {
public:
    LivelyMessageScreenshot() : IpcMessage(MessageType::msg_screenshot) {}
    LIVELY_IPC_JSON_IMPL(LivelyMessageScreenshot, IpcMessage) {
        // C# null string -> JSON null (Newtonsoft default NullValueHandling.Include).
        j["FileName"] = file_name.has_value() ? nlohmann::ordered_json(*file_name)
                                              : nlohmann::ordered_json(nullptr);
        j["Success"] = success;
    }
    std::optional<std::string> file_name; // std::nullopt encodes C# null
    bool success = false;
};

// public string Message; public ConsoleMessageType Category
class LivelyMessageConsole final : public IpcMessage {
public:
    LivelyMessageConsole() : IpcMessage(MessageType::msg_console) {}
    LIVELY_IPC_JSON_IMPL(LivelyMessageConsole, IpcMessage) {
        j["Message"] = message;
        j["Category"] = static_cast<int>(category);
    }
    std::string message;
    ConsoleMessageType category = ConsoleMessageType::log;
};

// ------------------------- livelyProperty control updates -------------------------

// public string Name; public double Value; public double Step
class LivelySlider final : public IpcMessage {
public:
    LivelySlider() : IpcMessage(MessageType::lp_slider) {}
    LIVELY_IPC_JSON_IMPL(LivelySlider, IpcMessage) {
        j["Name"] = name;
        j["Value"] = value;
        j["Step"] = step;
    }
    std::string name;
    double value = 0.0;
    double step = 0.0;
};

// public string Name; public string Value
class LivelyTextBox final : public IpcMessage {
public:
    LivelyTextBox() : IpcMessage(MessageType::lp_textbox) {}
    LIVELY_IPC_JSON_IMPL(LivelyTextBox, IpcMessage) {
        j["Name"] = name;
        j["Value"] = value;
    }
    std::string name;
    std::string value;
};

// public string Name; public int Value
class LivelyDropdown final : public IpcMessage {
public:
    LivelyDropdown() : IpcMessage(MessageType::lp_dropdown) {}
    LIVELY_IPC_JSON_IMPL(LivelyDropdown, IpcMessage) {
        j["Name"] = name;
        j["Value"] = value;
    }
    std::string name;
    int value = 0;
};

// public string Name; public bool IsDefault
class LivelyButton final : public IpcMessage {
public:
    LivelyButton() : IpcMessage(MessageType::lp_button) {}
    LIVELY_IPC_JSON_IMPL(LivelyButton, IpcMessage) {
        j["Name"] = name;
        j["IsDefault"] = is_default;
    }
    std::string name;
    bool is_default = false;
};

// public string Name; public string Value
class LivelyColorPicker final : public IpcMessage {
public:
    LivelyColorPicker() : IpcMessage(MessageType::lp_cpicker) {}
    LIVELY_IPC_JSON_IMPL(LivelyColorPicker, IpcMessage) {
        j["Name"] = name;
        j["Value"] = value;
    }
    std::string name;
    std::string value;
};

// public string Name; public int Value — MessageType.lp_dropdown_scaler.
// Sent by the UI when the user changes the scaler dropdown; the mpv host reads
// Value as the Lively WallpaperScaler ordinal (see MpvWallpaper::update_scaler).
class LivelyDropdownScaler final : public IpcMessage {
public:
    LivelyDropdownScaler() : IpcMessage(MessageType::lp_dropdown_scaler) {}
    LIVELY_IPC_JSON_IMPL(LivelyDropdownScaler, IpcMessage) {
        j["Name"] = name;
        j["Value"] = value;
    }
    std::string name;
    int value = 0;
};

// public string Name; public bool Value
class LivelyCheckbox final : public IpcMessage {
public:
    LivelyCheckbox() : IpcMessage(MessageType::lp_chekbox) {}
    LIVELY_IPC_JSON_IMPL(LivelyCheckbox, IpcMessage) {
        j["Name"] = name;
        j["Value"] = value;
    }
    std::string name;
    bool value = false;
};

#undef LIVELY_IPC_JSON_IMPL

// convenience: Newtonsoft-equivalent single-line serialization
// (compact, no spaces — matches JsonConvert.SerializeObject).
inline std::string serialize(const IpcMessage& msg) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    msg.to_json(j);
    return j.dump();
}

} // namespace lively::models
