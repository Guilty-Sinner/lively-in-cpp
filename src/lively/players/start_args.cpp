#include <lively/players/start_args.h>

#include <cstdlib>
#include <iterator>

namespace lively::players {

namespace {

using utility::OptionKind;
using utility::OptionSpec;
using utility::OptionValues;

// Enum member tables, in declaration order (ordinal == index).
constexpr const char* kAppTheme[] = {"Auto", "Light", "Dark"};
constexpr const char* kWebPageType[] = {"online", "local"};

// Reads an option as text; nullopt when it was not supplied (C# Default = null).
std::optional<std::string> text_of(const OptionValues& parsed, const char* name) {
    const auto it = parsed.values.find(name);
    if (it == parsed.values.end()) return std::nullopt;
    return it->second;
}

int int_of(const OptionValues& parsed, const char* name, int fallback) {
    const auto it = parsed.values.find(name);
    if (it == parsed.values.end()) return fallback;
    return std::atoi(it->second.c_str());
}

bool bool_of(const OptionValues& parsed, const char* name, bool fallback) {
    const auto it = parsed.values.find(name);
    if (it == parsed.values.end()) return fallback;
    // Switch values are normalised to "true"; Scalar bools carry their literal.
    return it->second != "false";
}

double double_of(const OptionValues& parsed, const char* name, double fallback) {
    const auto it = parsed.values.find(name);
    if (it == parsed.values.end()) return fallback;
    return std::strtod(it->second.c_str(), nullptr);
}

} // namespace

ParseOutcome<WmfStartArgs> ParseWmfStartArgs(const std::vector<std::string>& args) {
    static constexpr OptionSpec kOptions[] = {
        {"path", OptionKind::text, true},
        {"stretch", OptionKind::integer, false},
        {"volume", OptionKind::integer, false},
        {"property", OptionKind::text, false},
        {"verbose-log", OptionKind::boolean_switch, false},
    };

    const auto parsed = utility::ParseOptions(kOptions, std::size(kOptions), args);
    ParseOutcome<WmfStartArgs> outcome;
    if (!parsed.success) {
        outcome.error = parsed.error;
        return outcome;
    }

    WmfStartArgs out;
    out.file_path = text_of(parsed, "path").value_or("");
    out.stretch_mode = int_of(parsed, "stretch", 0);
    out.volume = int_of(parsed, "volume", 100);
    out.properties = text_of(parsed, "property");
    out.verbose_log = bool_of(parsed, "verbose-log", false);
    outcome.success = true;
    outcome.args = std::move(out);
    return outcome;
}

ParseOutcome<VlcStartArgs> ParseVlcStartArgs(const std::vector<std::string>& args) {
    static constexpr OptionSpec kOptions[] = {
        {"wallpaper-path", OptionKind::text, true},
        {"wallpaper-volume", OptionKind::integer, false},
        {"wallpaper-hardware-decoding", OptionKind::boolean_switch, false},
        {"wallpaper-property", OptionKind::text, false},
        {"wallpaper-geometry", OptionKind::text, false},
        {"wallpaper-color-scheme", OptionKind::enumeration, false, kAppTheme, std::size(kAppTheme)},
        {"wallpaper-verbose-log", OptionKind::boolean_switch, false},
    };

    const auto parsed = utility::ParseOptions(kOptions, std::size(kOptions), args);
    ParseOutcome<VlcStartArgs> outcome;
    if (!parsed.success) {
        outcome.error = parsed.error;
        return outcome;
    }

    VlcStartArgs out;
    out.file_path = text_of(parsed, "wallpaper-path").value_or("");
    out.volume = int_of(parsed, "wallpaper-volume", 100);
    out.hardware_decoding = bool_of(parsed, "wallpaper-hardware-decoding", true);
    out.properties = text_of(parsed, "wallpaper-property");
    out.geometry = text_of(parsed, "wallpaper-geometry");
    out.theme = static_cast<models::AppTheme>(int_of(parsed, "wallpaper-color-scheme", 0));
    out.verbose_log = bool_of(parsed, "wallpaper-verbose-log", false);
    outcome.success = true;
    outcome.args = std::move(out);
    return outcome;
}

ParseOutcome<WebView2StartArgs> ParseWebView2StartArgs(const std::vector<std::string>& args) {
    static constexpr OptionSpec kOptions[] = {
        {"wallpaper-url", OptionKind::text, true},
        {"wallpaper-property", OptionKind::text, false},
        {"wallpaper-type", OptionKind::enumeration, true, kWebPageType, std::size(kWebPageType)},
        {"wallpaper-display", OptionKind::text, false},
        {"wallpaper-geometry", OptionKind::text, false},
        {"wallpaper-scale", OptionKind::number, false},
        {"wallpaper-audio", OptionKind::boolean_switch, false},
        {"wallpaper-audio-id", OptionKind::text, false},
        {"wallpaper-debug", OptionKind::text, false},
        {"wallpaper-user-data", OptionKind::text, false},
        {"wallpaper-volume", OptionKind::integer, false},
        {"wallpaper-system-information", OptionKind::boolean_switch, false},
        {"wallpaper-system-nowplaying", OptionKind::boolean_switch, false},
        {"wallpaper-pause-event", OptionKind::boolean_switch, false},
        {"wallpaper-pause-media", OptionKind::boolean_switch, false},
        {"wallpaper-verbose-log", OptionKind::boolean_switch, false},
        {"wallpaper-color-scheme", OptionKind::enumeration, false, kAppTheme, std::size(kAppTheme)},
    };

    const auto parsed = utility::ParseOptions(kOptions, std::size(kOptions), args);
    ParseOutcome<WebView2StartArgs> outcome;
    if (!parsed.success) {
        outcome.error = parsed.error;
        return outcome;
    }

    WebView2StartArgs out;
    out.url = text_of(parsed, "wallpaper-url").value_or("");
    out.properties = text_of(parsed, "wallpaper-property");
    out.type = static_cast<models::WebPageType>(int_of(parsed, "wallpaper-type", 0));
    out.display_device = text_of(parsed, "wallpaper-display");
    out.geometry = text_of(parsed, "wallpaper-geometry");
    if (text_of(parsed, "wallpaper-scale")) out.scale = double_of(parsed, "wallpaper-scale", 0.0);
    out.audio_visualizer = bool_of(parsed, "wallpaper-audio", false);
    out.audio_visualizer_device_id = text_of(parsed, "wallpaper-audio-id");
    out.debug_port = text_of(parsed, "wallpaper-debug");
    out.user_data_path = text_of(parsed, "wallpaper-user-data");
    out.volume = int_of(parsed, "wallpaper-volume", 100);
    out.sys_info = bool_of(parsed, "wallpaper-system-information", false);
    out.now_playing = bool_of(parsed, "wallpaper-system-nowplaying", false);
    out.pause_event = bool_of(parsed, "wallpaper-pause-event", false);
    out.pause_web_media = bool_of(parsed, "wallpaper-pause-media", false);
    out.verbose_log = bool_of(parsed, "wallpaper-verbose-log", false);
    out.theme = static_cast<models::AppTheme>(int_of(parsed, "wallpaper-color-scheme", 0));
    outcome.success = true;
    outcome.args = std::move(out);
    return outcome;
}

ParseOutcome<CefSharpStartArgs> ParseCefSharpStartArgs(const std::vector<std::string>& args) {
    static constexpr OptionSpec kOptions[] = {
        {"wallpaper-url", OptionKind::text, true},
        {"wallpaper-property", OptionKind::text, false},
        {"wallpaper-type", OptionKind::enumeration, true, kWebPageType, std::size(kWebPageType)},
        {"wallpaper-display", OptionKind::text, true},
        {"wallpaper-geometry", OptionKind::text, false},
        {"wallpaper-audio", OptionKind::boolean_switch, false},
        {"wallpaper-audio-id", OptionKind::text, false},
        {"wallpaper-debug", OptionKind::text, false},
        {"wallpaper-cache", OptionKind::text, false},
        {"wallpaper-volume", OptionKind::integer, false},
        {"wallpaper-system-information", OptionKind::boolean_switch, false},
        {"wallpaper-system-nowplaying", OptionKind::boolean_switch, false},
        {"wallpaper-pause-event", OptionKind::boolean_switch, false},
        {"wallpaper-verbose-log", OptionKind::boolean_switch, false},
        {"wallpaper-color-scheme", OptionKind::enumeration, false, kAppTheme, std::size(kAppTheme)},
    };

    const auto parsed = utility::ParseOptions(kOptions, std::size(kOptions), args);
    ParseOutcome<CefSharpStartArgs> outcome;
    if (!parsed.success) {
        outcome.error = parsed.error;
        return outcome;
    }

    CefSharpStartArgs out;
    out.url = text_of(parsed, "wallpaper-url").value_or("");
    out.properties = text_of(parsed, "wallpaper-property");
    out.type = static_cast<models::WebPageType>(int_of(parsed, "wallpaper-type", 0));
    out.display_device = text_of(parsed, "wallpaper-display").value_or("");
    out.geometry = text_of(parsed, "wallpaper-geometry");
    out.audio_visualizer = bool_of(parsed, "wallpaper-audio", false);
    out.audio_visualizer_device_id = text_of(parsed, "wallpaper-audio-id");
    out.debug_port = text_of(parsed, "wallpaper-debug");
    out.cache_path = text_of(parsed, "wallpaper-cache");
    out.volume = int_of(parsed, "wallpaper-volume", 100);
    out.sys_info = bool_of(parsed, "wallpaper-system-information", false);
    out.now_playing = bool_of(parsed, "wallpaper-system-nowplaying", false);
    out.pause_event = bool_of(parsed, "wallpaper-pause-event", false);
    out.verbose_log = bool_of(parsed, "wallpaper-verbose-log", false);
    out.theme = static_cast<models::AppTheme>(int_of(parsed, "wallpaper-color-scheme", 0));
    outcome.success = true;
    outcome.args = std::move(out);
    return outcome;
}

} // namespace lively::players
