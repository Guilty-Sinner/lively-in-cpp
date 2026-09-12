#pragma once
// Port of Lively.Models/SettingsModel.cs — the complete user-settings model
// (settings.json persistence + SettingsService gRPC payload source).
//
// Equivalence rules:
//  * Every C# public property is a member with the SAME NAME (Newtonsoft
//    serializes properties in declaration order with their declared names;
//    this file preserves both).
//  * The default constructor reproduces the C# constructor value-for-value,
//    including the local-appdata WallpaperDir path.
//  * Fields the proto does NOT carry (GenerateTile, WaterMarkTile,
//    IgnoreUpdateTag, DisplayIdentification, ScreensaverGracePeriod,
//    ScreensaverLockWaitTimeout, TaskbarCrashTimeOutDelay,
//    ProcessMonitorGridTile*) are still part of the model — exactly like C#,
//    where UserSettingsClient's grpc mapping skips them too.
//
// Reference C#: Lively.Models/SettingsModel.cs

#include <lively/models/display_monitor.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace lively::models {

// ------------------------------ enums (C# ordinals) ------------------------------

enum class AppRules : int { pause = 0, ignore, kill };
enum class DisplayPause : int { perdisplay = 0, all };                    // DisplayPause.cs
enum class ProcessMonitorAlgorithm : int { foreground = 0, all, gamemode, grid };
enum class WallpaperScaler : int { none = 0, fill, uniform, uniformFill, autofit }; // C# member 'auto' (C++ keyword); ordinal 4 == proto autofit
enum class WallpaperArrangement : int { per = 0, span, duplicate };
enum class InputForwardMode : int { off = 0, mouse, mousekeyboard };
enum class LivelyMediaPlayer : int { wmf = 0, libvlc, libvlcExt, libmpv, libmpvExt, mpv, vlc };
enum class LivelyGifPlayer : int { win10Img = 0, libmpvExt, mpv, libvlcExt };
enum class LivelyPicturePlayer : int { picture = 0, winApi, mpv, wmf };
enum class LivelyWebBrowser : int { cef = 0, webview2 };
enum class LivelyGUIState : int { normal = 0, lite, headless };
enum class StreamQualitySuggestion : int { Lowest = 0, Low, LowMedium, Medium, MediumHigh, High, Highest };
enum class AppTheme : int { Auto = 0, Light, Dark };                     // C# capitalised members
enum class AppThemeBackground : int { default_mica = 0, default_acrylic, dynamic, custom };
enum class TaskbarTheme : int { none = 0, clear, blur, fluent, color, wallpaper, wallpaperFluent };
enum class ScreensaverType : int { wallpaper = 0, different };
// NOTE: C# ScreensaverIdleTime values are MINUTES (none=0, min1=1 ... min120=120);
// the proto enum is a positional index (off_=0..min120=12). UserSettingsClient
// casts ((uint)delay) — the C++ port keeps the raw C# value and converts at the
// grpc boundary, like the C# code.
enum class ScreensaverIdleTime : int {
    none = 0, min1 = 1, min2 = 2, min3 = 3, min5 = 5, min10 = 10, min15 = 15,
    min20 = 20, min25 = 25, min30 = 30, min45 = 45, min60 = 60, min120 = 120,
};
enum class TargetColorspaceHintMode : int { target = 0, source, sourceDynamic };
enum class DisplayAudioMode : int { selection = 0, all };
enum class DisplayIdentificationMode : int { deviceName = 0, deviceId, screenLayout }; // local-only (not in proto)

// ------------------------------ SettingsModel ------------------------------

class SettingsModel {
public:
    SettingsModel(); // C#-identical defaults

    // Newtonsoft-equivalent serialization (declaration order, property names).
    std::string to_json_string() const;
    static SettingsModel from_json_string(const std::string& json);

    // ---- properties (declaration order mirrors the C# file) ----
    std::string app_version;
    std::string app_previous_version;
    std::string language;
    bool startup = false;
    bool generate_tile = false;            // C# GenerateTile
    bool lively_zip_generate = false;
    bool water_mark_tile = false;          // C# WaterMarkTile
    bool is_first_run = false;
    bool control_panel_opened = false;
    AppRules app_focus_pause = AppRules::ignore;
    AppRules app_fullscreen_pause = AppRules::pause;
    AppRules battery_pause = AppRules::ignore;
    AppRules remote_desktop_pause = AppRules::pause;
    AppRules power_save_mode_pause = AppRules::ignore;
    DisplayPause display_pause_settings = DisplayPause::perdisplay;
    ProcessMonitorAlgorithm process_monitor_algorithm = ProcessMonitorAlgorithm::foreground;
    bool live_tile = false;
    WallpaperScaler scaler_video = WallpaperScaler::none;
    WallpaperScaler scaler_gif = WallpaperScaler::none;
    WallpaperArrangement wallpaper_arrangement = WallpaperArrangement::per;
    std::string saved_url;
    std::optional<std::string> ignore_update_tag;  // C# null default → JSON null
    int process_timer_interval = 0;
    int wallpaper_wait_time = 0;
    bool safe_shutdown = false;
    bool is_restart = false;
    InputForwardMode input_forward = InputForwardMode::off;
    bool mouse_input_mov_always = false;
    int tile_size = 0;
    DisplayIdentificationMode display_identification = DisplayIdentificationMode::deviceId;
    LivelyMediaPlayer video_player = LivelyMediaPlayer::wmf;
    bool video_player_hw_accel = false;
    LivelyGifPlayer gif_player = LivelyGifPlayer::win10Img;
    LivelyPicturePlayer picture_player = LivelyPicturePlayer::picture;
    LivelyWebBrowser web_browser = LivelyWebBrowser::cef;
    bool gif_capture = false;
    bool multi_file_auto_import = false;
    std::shared_ptr<DisplayMonitor> selected_display;      // C# null until set
    LivelyGUIState ui_mode = LivelyGUIState::normal;
    std::string wallpaper_dir;
    bool wallpaper_dir_move_existing_wallpaper_new_dir = false;
    bool sys_tray_icon = false;
    bool auto_detect_online_streams = false;
    bool extract_stream_meta_data = false;
    std::string web_debug_port;
    int wallpaper_bundle_version = 0;
    StreamQualitySuggestion stream_quality = StreamQualitySuggestion::Lowest;
    int audio_volume_global = 0;
    bool audio_only_on_desktop = false;
    WallpaperScaler wallpaper_scaling = WallpaperScaler::none;
    bool cef_disk_cache = false;
    bool debug_menu = false;
    bool is_beta_opt_in = false;
    AppTheme application_theme = AppTheme::Auto;
    bool lock_screen_auto_wallpaper = false;
    bool desktop_auto_wallpaper = false;
    TaskbarTheme system_taskbar_theme = TaskbarTheme::none;
    ScreensaverType screensaver_type = ScreensaverType::wallpaper;
    WallpaperArrangement screensaver_arragement = WallpaperArrangement::per; // [sic] C# typo
    ScreensaverIdleTime screensaver_idle_delay = ScreensaverIdleTime::none;
    bool screensaver_oled_warning = false;
    bool screensaver_empty_screen_show_black = false;
    bool screensaver_lock_on_resume = false;
    int screensaver_global_volume = 0;
    bool screensaver_fade_in = false;
    int screensaver_grace_period = 0;
    int screensaver_lock_wait_timeout = 0;
    bool keep_awake_ui = false;
    bool remember_selected_screen = false;
    bool is_updated = false;
    bool is_updated_notify = false;
    std::string visualizer_audio_device_id;
    bool is_screensaver_plugin_notify = false;
    std::optional<std::string> application_theme_background_path; // C# null default → JSON null
    AppThemeBackground application_theme_background = AppThemeBackground::default_mica;
    int theme_bundle_version = 0;
    int taskbar_crash_time_out_delay = 0;
    double process_monitor_grid_tile_coverage_threshold = 0.0;
    int process_monitor_grid_tile_size = 0;
    TargetColorspaceHintMode video_target_color_space_mode = TargetColorspaceHintMode::target;
    DisplayAudioMode display_audio_output = DisplayAudioMode::selection;
    std::shared_ptr<DisplayMonitor> selected_audio_output_display; // C# null until set
    bool is_restart_after_lockscreen = false;
};

} // namespace lively::models
