#include <lively/models/settings_model.h>

#include <lively/common/constants.h>

#include <nlohmann/json.hpp>

namespace lively::models {

namespace {

using json = nlohmann::ordered_json;

json rect_to_json(const Rectangle& r);
Rectangle rect_from_json(const json& j);
json display_to_json(const std::shared_ptr<DisplayMonitor>& d);
std::shared_ptr<DisplayMonitor> display_from_json(const json& j);

template <typename E>
E read_enum(const json& j, const char* key, E fallback) {
    if (!j.contains(key) || j.at(key).is_null()) return fallback;
    return static_cast<E>(j.at(key).get<int>());
}

// member writers — name strings match the C# property names exactly
void write_members(const SettingsModel& m, json& j) {
    j["AppVersion"] = m.app_version;
    j["AppPreviousVersion"] = m.app_previous_version;
    j["Language"] = m.language;
    j["Startup"] = m.startup;
    j["GenerateTile"] = m.generate_tile;
    j["LivelyZipGenerate"] = m.lively_zip_generate;
    j["WaterMarkTile"] = m.water_mark_tile;
    j["IsFirstRun"] = m.is_first_run;
    j["ControlPanelOpened"] = m.control_panel_opened;
    j["AppFocusPause"] = static_cast<int>(m.app_focus_pause);
    j["AppFullscreenPause"] = static_cast<int>(m.app_fullscreen_pause);
    j["BatteryPause"] = static_cast<int>(m.battery_pause);
    j["RemoteDesktopPause"] = static_cast<int>(m.remote_desktop_pause);
    j["PowerSaveModePause"] = static_cast<int>(m.power_save_mode_pause);
    j["DisplayPauseSettings"] = static_cast<int>(m.display_pause_settings);
    j["ProcessMonitorAlgorithm"] = static_cast<int>(m.process_monitor_algorithm);
    j["LiveTile"] = m.live_tile;
    j["ScalerVideo"] = static_cast<int>(m.scaler_video);
    j["ScalerGif"] = static_cast<int>(m.scaler_gif);
    j["WallpaperArrangement"] = static_cast<int>(m.wallpaper_arrangement);
    j["SavedURL"] = m.saved_url;
    // C# null string → JSON null (Newtonsoft default NullValueHandling.Include).
    j["IgnoreUpdateTag"] = m.ignore_update_tag.has_value()
                                ? json(*m.ignore_update_tag)
                                : json(nullptr);
    j["ProcessTimerInterval"] = m.process_timer_interval;
    j["WallpaperWaitTime"] = m.wallpaper_wait_time;
    j["SafeShutdown"] = m.safe_shutdown;
    j["IsRestart"] = m.is_restart;
    j["InputForward"] = static_cast<int>(m.input_forward);
    j["MouseInputMovAlways"] = m.mouse_input_mov_always;
    j["TileSize"] = m.tile_size;
    j["DisplayIdentification"] = static_cast<int>(m.display_identification);
    j["VideoPlayer"] = static_cast<int>(m.video_player);
    j["VideoPlayerHwAccel"] = m.video_player_hw_accel;
    j["GifPlayer"] = static_cast<int>(m.gif_player);
    j["PicturePlayer"] = static_cast<int>(m.picture_player);
    j["WebBrowser"] = static_cast<int>(m.web_browser);
    j["GifCapture"] = m.gif_capture;
    j["MultiFileAutoImport"] = m.multi_file_auto_import;
    j["SelectedDisplay"] = display_to_json(m.selected_display);
    j["UIMode"] = static_cast<int>(m.ui_mode);
    j["WallpaperDir"] = m.wallpaper_dir;
    j["WallpaperDirMoveExistingWallpaperNewDir"] = m.wallpaper_dir_move_existing_wallpaper_new_dir;
    j["SysTrayIcon"] = m.sys_tray_icon;
    j["AutoDetectOnlineStreams"] = m.auto_detect_online_streams;
    j["ExtractStreamMetaData"] = m.extract_stream_meta_data;
    j["WebDebugPort"] = m.web_debug_port;
    j["WallpaperBundleVersion"] = m.wallpaper_bundle_version;
    j["StreamQuality"] = static_cast<int>(m.stream_quality);
    j["AudioVolumeGlobal"] = m.audio_volume_global;
    j["AudioOnlyOnDesktop"] = m.audio_only_on_desktop;
    j["WallpaperScaling"] = static_cast<int>(m.wallpaper_scaling);
    j["CefDiskCache"] = m.cef_disk_cache;
    j["DebugMenu"] = m.debug_menu;
    j["IsBetaOptIn"] = m.is_beta_opt_in;
    j["ApplicationTheme"] = static_cast<int>(m.application_theme);
    j["LockScreenAutoWallpaper"] = m.lock_screen_auto_wallpaper;
    j["DesktopAutoWallpaper"] = m.desktop_auto_wallpaper;
    j["SystemTaskbarTheme"] = static_cast<int>(m.system_taskbar_theme);
    j["ScreensaverType"] = static_cast<int>(m.screensaver_type);
    j["ScreensaverArragement"] = static_cast<int>(m.screensaver_arragement); // [sic]
    j["ScreensaverIdleDelay"] = static_cast<int>(m.screensaver_idle_delay);
    j["ScreensaverOledWarning"] = m.screensaver_oled_warning;
    j["ScreensaverEmptyScreenShowBlack"] = m.screensaver_empty_screen_show_black;
    j["ScreensaverLockOnResume"] = m.screensaver_lock_on_resume;
    j["ScreensaverGlobalVolume"] = m.screensaver_global_volume;
    j["ScreensaverFadeIn"] = m.screensaver_fade_in;
    j["ScreensaverGracePeriod"] = m.screensaver_grace_period;
    j["ScreensaverLockWaitTimeout"] = m.screensaver_lock_wait_timeout;
    j["KeepAwakeUI"] = m.keep_awake_ui;
    j["RememberSelectedScreen"] = m.remember_selected_screen;
    j["IsUpdated"] = m.is_updated;
    j["IsUpdatedNotify"] = m.is_updated_notify;
    j["VisualizerAudioDeviceId"] = m.visualizer_audio_device_id;
    j["IsScreensaverPluginNotify"] = m.is_screensaver_plugin_notify;
    j["ApplicationThemeBackgroundPath"] = m.application_theme_background_path.has_value()
                                              ? json(*m.application_theme_background_path)
                                              : json(nullptr);
    j["ApplicationThemeBackground"] = static_cast<int>(m.application_theme_background);
    j["ThemeBundleVersion"] = m.theme_bundle_version;
    j["TaskbarCrashTimeOutDelay"] = m.taskbar_crash_time_out_delay;
    j["ProcessMonitorGridTileCoverageThreshold"] = m.process_monitor_grid_tile_coverage_threshold;
    j["ProcessMonitorGridTileSize"] = m.process_monitor_grid_tile_size;
    j["VideoTargetColorSpaceMode"] = static_cast<int>(m.video_target_color_space_mode);
    j["DisplayAudioOutput"] = static_cast<int>(m.display_audio_output);
    j["SelectedAudioOutputDisplay"] = display_to_json(m.selected_audio_output_display);
    j["IsRestartAfterLockscreen"] = m.is_restart_after_lockscreen;
}

json display_to_json(const std::shared_ptr<DisplayMonitor>& d) {
    if (!d) return json(nullptr);
    json j;
    j["DeviceId"] = d->device_id;
    j["DeviceName"] = d->device_name;
    j["DisplayName"] = d->display_name;
    j["HMonitor"] = d->h_monitor;
    j["IsPrimary"] = d->is_primary;
    j["Index"] = d->index;
    j["Bounds"] = rect_to_json(d->bounds);
    j["WorkingArea"] = rect_to_json(d->working_area);
    return j;
}

std::shared_ptr<DisplayMonitor> display_from_json(const json& j) {
    if (j.is_null()) return nullptr;
    auto d = std::make_shared<DisplayMonitor>();
    d->device_id = j.value("DeviceId", std::string());
    d->device_name = j.value("DeviceName", std::string());
    d->display_name = j.value("DisplayName", std::string());
    d->h_monitor = j.value("HMonitor", std::int64_t{0});
    d->is_primary = j.value("IsPrimary", false);
    d->index = j.value("Index", 0);
    d->bounds = rect_from_json(j.value("Bounds", json::object()));
    d->working_area = rect_from_json(j.value("WorkingArea", json::object()));
    return d;
}

json rect_to_json(const Rectangle& r) {
    json j;
    j["X"] = r.x;
    j["Y"] = r.y;
    j["Width"] = r.width;
    j["Height"] = r.height;
    return j;
}

Rectangle rect_from_json(const json& j) {
    Rectangle r;
    r.x = j.value("X", 0);
    r.y = j.value("Y", 0);
    r.width = j.value("Width", 0);
    r.height = j.value("Height", 0);
    return r;
}



} // namespace

SettingsModel::SettingsModel() {
    // C# SettingsModel() constructor — value-for-value.
    saved_url = "https://www.youtube.com/watch?v=aqz-KE-bpKQ";
    process_monitor_algorithm = ProcessMonitorAlgorithm::grid;
    wallpaper_arrangement = WallpaperArrangement::per;
    screensaver_arragement = WallpaperArrangement::per;
    screensaver_type = ScreensaverType::wallpaper;
    screensaver_grace_period = 5;
    screensaver_lock_wait_timeout = 5;
    // C# AppVersion = entry-assembly version; C++ port stamps the C#-format
    // value the caller supplies via Constants/CLI — default mirrors "0.0.0.0".
    app_version = "0.0.0.0";
    app_previous_version = std::string();
    startup = true;
    is_first_run = true;
    control_panel_opened = false;
    app_focus_pause = AppRules::ignore;
    app_fullscreen_pause = AppRules::pause;
    battery_pause = AppRules::ignore;
    video_player = LivelyMediaPlayer::mpv;
    video_player_hw_accel = true;
    web_browser = LivelyWebBrowser::webview2;
    gif_player = LivelyGifPlayer::mpv;
    picture_player = LivelyPicturePlayer::mpv;
    process_monitor_grid_tile_coverage_threshold = 0.05;
    process_monitor_grid_tile_size = 50;
    wallpaper_wait_time = 20000;      // 20sec
    process_timer_interval = 500;     // quicker response note in C#
    stream_quality = StreamQualitySuggestion::High;
    generate_tile = true;
    lively_zip_generate = false;
    water_mark_tile = true;
    ignore_update_tag = std::nullopt; // C# null
    scaler_video = WallpaperScaler::fill;
    scaler_gif = WallpaperScaler::fill;
    gif_capture = true;
    multi_file_auto_import = true;
    safe_shutdown = true;
    is_restart = false;
    input_forward = InputForwardMode::mouse;
    mouse_input_mov_always = true;
    tile_size = 1;
    display_identification = DisplayIdentificationMode::deviceId;
    ui_mode = LivelyGUIState::normal;
    // C# Path.Combine(LocalApplicationData, "Lively Wallpaper", "Library")
    wallpaper_dir = common::UserLocalAppDataDir() + "\\Lively Wallpaper\\Library";
    wallpaper_dir_move_existing_wallpaper_new_dir = true;
    sys_tray_icon = true;
    web_debug_port = std::string();
    auto_detect_online_streams = true;
    extract_stream_meta_data = true;
    wallpaper_bundle_version = -1;
    theme_bundle_version = -1;
    audio_volume_global = 75;
    audio_only_on_desktop = true;
    wallpaper_scaling = WallpaperScaler::fill;
    cef_disk_cache = false;
    debug_menu = false;
    is_beta_opt_in = false;
    application_theme = AppTheme::Dark;
    remote_desktop_pause = AppRules::pause;
    power_save_mode_pause = AppRules::ignore;
    lock_screen_auto_wallpaper = false;
    desktop_auto_wallpaper = false;
    system_taskbar_theme = TaskbarTheme::none;
    screensaver_idle_delay = ScreensaverIdleTime::none;
    screensaver_oled_warning = false;
    screensaver_empty_screen_show_black = true;
    screensaver_lock_on_resume = false;
    screensaver_global_volume = 0;
    screensaver_fade_in = true;
    keep_awake_ui = false;
    remember_selected_screen = true;
    is_updated = false;
    is_updated_notify = false;
    is_screensaver_plugin_notify = true;
    application_theme_background_path = std::nullopt; // C# null
    application_theme_background = AppThemeBackground::default_mica;
    taskbar_crash_time_out_delay = 30;
    language = std::string();
    video_target_color_space_mode = TargetColorspaceHintMode::target;
    visualizer_audio_device_id = std::string();
    display_audio_output = DisplayAudioMode::all;
    is_restart_after_lockscreen = false;
}

std::string SettingsModel::to_json_string() const {
    json j = json::object();
    write_members(*this, j);
    return j.dump(); // compact — JsonConvert.SerializeObject
}

SettingsModel SettingsModel::from_json_string(const std::string& raw) {
    SettingsModel out; // start from C# defaults (missing keys keep defaults)
    json j = json::parse(raw);
    out.app_version = j.value("AppVersion", out.app_version);
    out.app_previous_version = j.value("AppPreviousVersion", out.app_previous_version);
    out.language = j.value("Language", out.language);
    out.startup = j.value("Startup", out.startup);
    out.generate_tile = j.value("GenerateTile", out.generate_tile);
    out.lively_zip_generate = j.value("LivelyZipGenerate", out.lively_zip_generate);
    out.water_mark_tile = j.value("WaterMarkTile", out.water_mark_tile);
    out.is_first_run = j.value("IsFirstRun", out.is_first_run);
    out.control_panel_opened = j.value("ControlPanelOpened", out.control_panel_opened);
    out.app_focus_pause = read_enum(j, "AppFocusPause", out.app_focus_pause);
    out.app_fullscreen_pause = read_enum(j, "AppFullscreenPause", out.app_fullscreen_pause);
    out.battery_pause = read_enum(j, "BatteryPause", out.battery_pause);
    out.remote_desktop_pause = read_enum(j, "RemoteDesktopPause", out.remote_desktop_pause);
    out.power_save_mode_pause = read_enum(j, "PowerSaveModePause", out.power_save_mode_pause);
    out.display_pause_settings = read_enum(j, "DisplayPauseSettings", out.display_pause_settings);
    out.process_monitor_algorithm = read_enum(j, "ProcessMonitorAlgorithm", out.process_monitor_algorithm);
    out.live_tile = j.value("LiveTile", out.live_tile);
    out.scaler_video = read_enum(j, "ScalerVideo", out.scaler_video);
    out.scaler_gif = read_enum(j, "ScalerGif", out.scaler_gif);
    out.wallpaper_arrangement = read_enum(j, "WallpaperArrangement", out.wallpaper_arrangement);
    out.saved_url = j.value("SavedURL", out.saved_url);
    if (j.contains("IgnoreUpdateTag") && !j.at("IgnoreUpdateTag").is_null()) {
        out.ignore_update_tag = j.at("IgnoreUpdateTag").get<std::string>();
    }
    out.process_timer_interval = j.value("ProcessTimerInterval", out.process_timer_interval);
    out.wallpaper_wait_time = j.value("WallpaperWaitTime", out.wallpaper_wait_time);
    out.safe_shutdown = j.value("SafeShutdown", out.safe_shutdown);
    out.is_restart = j.value("IsRestart", out.is_restart);
    out.input_forward = read_enum(j, "InputForward", out.input_forward);
    out.mouse_input_mov_always = j.value("MouseInputMovAlways", out.mouse_input_mov_always);
    out.tile_size = j.value("TileSize", out.tile_size);
    out.display_identification = read_enum(j, "DisplayIdentification", out.display_identification);
    out.video_player = read_enum(j, "VideoPlayer", out.video_player);
    out.video_player_hw_accel = j.value("VideoPlayerHwAccel", out.video_player_hw_accel);
    out.gif_player = read_enum(j, "GifPlayer", out.gif_player);
    out.picture_player = read_enum(j, "PicturePlayer", out.picture_player);
    out.web_browser = read_enum(j, "WebBrowser", out.web_browser);
    out.gif_capture = j.value("GifCapture", out.gif_capture);
    out.multi_file_auto_import = j.value("MultiFileAutoImport", out.multi_file_auto_import);
    out.selected_display = display_from_json(j.value("SelectedDisplay", json(nullptr)));
    out.ui_mode = read_enum(j, "UIMode", out.ui_mode);
    out.wallpaper_dir = j.value("WallpaperDir", out.wallpaper_dir);
    out.wallpaper_dir_move_existing_wallpaper_new_dir =
        j.value("WallpaperDirMoveExistingWallpaperNewDir", out.wallpaper_dir_move_existing_wallpaper_new_dir);
    out.sys_tray_icon = j.value("SysTrayIcon", out.sys_tray_icon);
    out.auto_detect_online_streams = j.value("AutoDetectOnlineStreams", out.auto_detect_online_streams);
    out.extract_stream_meta_data = j.value("ExtractStreamMetaData", out.extract_stream_meta_data);
    out.web_debug_port = j.value("WebDebugPort", out.web_debug_port);
    out.wallpaper_bundle_version = j.value("WallpaperBundleVersion", out.wallpaper_bundle_version);
    out.stream_quality = read_enum(j, "StreamQuality", out.stream_quality);
    out.audio_volume_global = j.value("AudioVolumeGlobal", out.audio_volume_global);
    out.audio_only_on_desktop = j.value("AudioOnlyOnDesktop", out.audio_only_on_desktop);
    out.wallpaper_scaling = read_enum(j, "WallpaperScaling", out.wallpaper_scaling);
    out.cef_disk_cache = j.value("CefDiskCache", out.cef_disk_cache);
    out.debug_menu = j.value("DebugMenu", out.debug_menu);
    out.is_beta_opt_in = j.value("IsBetaOptIn", out.is_beta_opt_in);
    out.application_theme = read_enum(j, "ApplicationTheme", out.application_theme);
    out.lock_screen_auto_wallpaper = j.value("LockScreenAutoWallpaper", out.lock_screen_auto_wallpaper);
    out.desktop_auto_wallpaper = j.value("DesktopAutoWallpaper", out.desktop_auto_wallpaper);
    out.system_taskbar_theme = read_enum(j, "SystemTaskbarTheme", out.system_taskbar_theme);
    out.screensaver_type = read_enum(j, "ScreensaverType", out.screensaver_type);
    out.screensaver_arragement = read_enum(j, "ScreensaverArragement", out.screensaver_arragement);
    out.screensaver_idle_delay = read_enum(j, "ScreensaverIdleDelay", out.screensaver_idle_delay);
    out.screensaver_oled_warning = j.value("ScreensaverOledWarning", out.screensaver_oled_warning);
    out.screensaver_empty_screen_show_black = j.value("ScreensaverEmptyScreenShowBlack", out.screensaver_empty_screen_show_black);
    out.screensaver_lock_on_resume = j.value("ScreensaverLockOnResume", out.screensaver_lock_on_resume);
    out.screensaver_global_volume = j.value("ScreensaverGlobalVolume", out.screensaver_global_volume);
    out.screensaver_fade_in = j.value("ScreensaverFadeIn", out.screensaver_fade_in);
    out.screensaver_grace_period = j.value("ScreensaverGracePeriod", out.screensaver_grace_period);
    out.screensaver_lock_wait_timeout = j.value("ScreensaverLockWaitTimeout", out.screensaver_lock_wait_timeout);
    out.keep_awake_ui = j.value("KeepAwakeUI", out.keep_awake_ui);
    out.remember_selected_screen = j.value("RememberSelectedScreen", out.remember_selected_screen);
    out.is_updated = j.value("IsUpdated", out.is_updated);
    out.is_updated_notify = j.value("IsUpdatedNotify", out.is_updated_notify);
    out.visualizer_audio_device_id = j.value("VisualizerAudioDeviceId", out.visualizer_audio_device_id);
    out.is_screensaver_plugin_notify = j.value("IsScreensaverPluginNotify", out.is_screensaver_plugin_notify);
    if (j.contains("ApplicationThemeBackgroundPath") && !j.at("ApplicationThemeBackgroundPath").is_null()) {
        out.application_theme_background_path = j.at("ApplicationThemeBackgroundPath").get<std::string>();
    }
    out.application_theme_background = read_enum(j, "ApplicationThemeBackground", out.application_theme_background);
    out.theme_bundle_version = j.value("ThemeBundleVersion", out.theme_bundle_version);
    out.taskbar_crash_time_out_delay = j.value("TaskbarCrashTimeOutDelay", out.taskbar_crash_time_out_delay);
    out.process_monitor_grid_tile_coverage_threshold =
        j.value("ProcessMonitorGridTileCoverageThreshold", out.process_monitor_grid_tile_coverage_threshold);
    out.process_monitor_grid_tile_size = j.value("ProcessMonitorGridTileSize", out.process_monitor_grid_tile_size);
    out.video_target_color_space_mode = read_enum(j, "VideoTargetColorSpaceMode", out.video_target_color_space_mode);
    out.display_audio_output = read_enum(j, "DisplayAudioOutput", out.display_audio_output);
    out.selected_audio_output_display = display_from_json(j.value("SelectedAudioOutputDisplay", json(nullptr)));
    out.is_restart_after_lockscreen = j.value("IsRestartAfterLockscreen", out.is_restart_after_lockscreen);
    return out;
}

} // namespace lively::models
