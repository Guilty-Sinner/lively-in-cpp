#pragma once
// Port of the four Lively.Player.* /StartArgs.cs classes — the contract the
// Lively core uses when it launches a wallpaper player process.
//
// Each player is a separate process started with `--option value` arguments and
// parsed with CommandLineParser ([Option] attributes). The port reuses the
// oracle-verified parser in lively/utility/commandline.h, so the semantics that
// matter are preserved exactly:
//
//   * `string` / `int` / `bool?` are TargetType.Scalar — they need a value;
//     a bare name binds nothing and stays at its Default (that is why an
//     unpaired "--wallpaper-volume" silently yields 100, not 0).
//   * `bool` is TargetType.Switch — *presence* means true and any attached
//     value is ignored, so "--wallpaper-hardware-decoding false" still yields
//     `true`. Vlc's HardwareDecoding therefore defaults to true and cannot be
//     turned off from the command line; that is genuinely what the C# player
//     does (oracle-verified).
//   * `enum` values are matched case-SENSITIVELY, and only defined members
//     (by name or ordinal) are accepted.
//   * `Required = true` options missing from the command line produce
//     MissingRequiredOptionError instead of defaults.

#include <lively/models/settings_model.h>  // AppTheme
#include <lively/models/wallpaper_type.h>  // WebPageType
#include <lively/utility/commandline.h>

#include <optional>
#include <string>
#include <vector>

namespace lively::players {

// Successful parse: the option values, with the C# `Default = ...` values for
// options that were not supplied.
template <typename T>
struct ParseOutcome {
    bool success = false;
    T args{};
    std::string error;  // canonical CommandLineParser tag list, e.g. "MissingRequiredOptionError"
};

// ---- Lively.Player.Wmf/StartArgs.cs ----
struct WmfStartArgs {
    std::string file_path;                  // --path (required)
    int stretch_mode = 0;                   // --stretch
    int volume = 100;                       // --volume
    std::optional<std::string> properties;  // --property
    bool verbose_log = false;               // --verbose-log (switch)
};

// ---- Lively.Player.Vlc/StartArgs.cs ----
struct VlcStartArgs {
    std::string file_path;                  // --wallpaper-path (required)
    int volume = 100;                       // --wallpaper-volume
    bool hardware_decoding = true;          // --wallpaper-hardware-decoding (switch)
    std::optional<std::string> properties;  // --wallpaper-property
    std::optional<std::string> geometry;    // --wallpaper-geometry
    models::AppTheme theme = models::AppTheme::Auto;  // --wallpaper-color-scheme
    bool verbose_log = false;               // --wallpaper-verbose-log (switch)
};

// ---- Lively.Player.WebView2/StartArgs.cs ----
struct WebView2StartArgs {
    std::string url;                                  // --wallpaper-url (required)
    std::optional<std::string> properties;            // --wallpaper-property
    models::WebPageType type = models::WebPageType::online;  // --wallpaper-type (required)
    std::optional<std::string> display_device;        // --wallpaper-display
    std::optional<std::string> geometry;              // --wallpaper-geometry
    std::optional<double> scale;                      // --wallpaper-scale
    bool audio_visualizer = false;                    // --wallpaper-audio (switch)
    std::optional<std::string> audio_visualizer_device_id;  // --wallpaper-audio-id
    std::optional<std::string> debug_port;            // --wallpaper-debug
    std::optional<std::string> user_data_path;        // --wallpaper-user-data
    int volume = 100;                                 // --wallpaper-volume
    bool sys_info = false;                            // --wallpaper-system-information (switch)
    bool now_playing = false;                         // --wallpaper-system-nowplaying (switch)
    bool pause_event = false;                         // --wallpaper-pause-event (switch)
    bool pause_web_media = false;                     // --wallpaper-pause-media (switch)
    bool verbose_log = false;                         // --wallpaper-verbose-log (switch)
    models::AppTheme theme = models::AppTheme::Auto;  // --wallpaper-color-scheme
};

// ---- Lively.Player.CefSharp/StartArgs.cs ----
struct CefSharpStartArgs {
    std::string url;                                  // --wallpaper-url (required)
    std::optional<std::string> properties;            // --wallpaper-property
    models::WebPageType type = models::WebPageType::online;  // --wallpaper-type (required)
    std::string display_device;                       // --wallpaper-display (required)
    std::optional<std::string> geometry;              // --wallpaper-geometry
    bool audio_visualizer = false;                    // --wallpaper-audio (switch)
    std::optional<std::string> audio_visualizer_device_id;  // --wallpaper-audio-id
    std::optional<std::string> debug_port;            // --wallpaper-debug
    std::optional<std::string> cache_path;            // --wallpaper-cache
    int volume = 100;                                 // --wallpaper-volume
    bool sys_info = false;                            // --wallpaper-system-information (switch)
    bool now_playing = false;                         // --wallpaper-system-nowplaying (switch)
    bool pause_event = false;                         // --wallpaper-pause-event (switch)
    bool verbose_log = false;                         // --wallpaper-verbose-log (switch)
    models::AppTheme theme = models::AppTheme::Auto;  // --wallpaper-color-scheme
};

ParseOutcome<WmfStartArgs> ParseWmfStartArgs(const std::vector<std::string>& args);
ParseOutcome<VlcStartArgs> ParseVlcStartArgs(const std::vector<std::string>& args);
ParseOutcome<WebView2StartArgs> ParseWebView2StartArgs(const std::vector<std::string>& args);
ParseOutcome<CefSharpStartArgs> ParseCefSharpStartArgs(const std::vector<std::string>& args);

} // namespace lively::players
