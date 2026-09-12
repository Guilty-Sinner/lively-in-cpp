#include <lively/rpc/user_settings_client.h>

#include <grpcpp/grpcpp.h>

#include <settings.grpc.pb.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace lively::rpc {

namespace {

using Lively::Grpc::Common::Proto::Settings::AppRulesDataModel;
using Lively::Grpc::Common::Proto::Settings::AppRulesSettings;
using Lively::Grpc::Common::Proto::Settings::GetScreensResponse;
using Lively::Grpc::Common::Proto::Settings::Rectangle;
using Lively::Grpc::Common::Proto::Settings::SettingsDataModel;
using Lively::Grpc::Common::Proto::Settings::SettingsService;
using Empty = google::protobuf::Empty;

std::runtime_error rpc_error(const grpc::Status& s) {
    return std::runtime_error("RpcException: " + s.error_message() +
                              " (code " + std::to_string(static_cast<int>(s.error_code())) + ")");
}

models::Rectangle to_rect(const Rectangle& r) {
    return {r.x(), r.y(), r.width(), r.height()};
}

Rectangle from_rect(const models::Rectangle& r) {
    Rectangle out;
    out.set_x(r.x);
    out.set_y(r.y);
    out.set_width(r.width);
    out.set_height(r.height);
    return out;
}

// proto GetScreensResponse → DisplayMonitor (C# CreateSettingsFromGrpc shape).
std::shared_ptr<models::DisplayMonitor> to_display(const GetScreensResponse& s) {
    auto d = std::make_shared<models::DisplayMonitor>();
    d->device_id = s.device_id();
    d->device_name = s.device_name();
    d->display_name = s.display_name();
    d->h_monitor = s.h_monitor();
    d->is_primary = s.is_primary();
    d->index = s.index();
    d->bounds = to_rect(s.bounds());
    d->working_area = to_rect(s.working_area());
    return d;
}

GetScreensResponse from_display(const models::DisplayMonitor& d) {
    GetScreensResponse s;
    s.set_device_id(d.device_id);
    s.set_device_name(d.device_name);
    s.set_display_name(d.display_name);
    s.set_h_monitor(static_cast<std::int32_t>(d.h_monitor));
    s.set_is_primary(d.is_primary);
    s.set_index(d.index);
    *s.mutable_working_area() = from_rect(d.working_area);
    *s.mutable_bounds() = from_rect(d.bounds);
    return s;
}

// C# CreateSettingsFromGrpc — proto → SettingsModel (field-for-field).
models::SettingsModel from_proto(const SettingsDataModel& s) {
    models::SettingsModel m; // defaults; C# object-initializer leaves unset fields default
    m.saved_url = s.saved_url();
    // C#: (ProcessMonitorAlgorithm)((int)settings.ProcessMonitorAlogorithm)
    m.process_monitor_algorithm = static_cast<models::ProcessMonitorAlgorithm>(static_cast<int>(s.process_monitor_alogorithm()));
    m.wallpaper_arrangement = static_cast<models::WallpaperArrangement>(static_cast<int>(s.wallpaper_arrangement()));
    m.screensaver_arragement = static_cast<models::WallpaperArrangement>(static_cast<int>(s.screensaver_arrangement()));
    m.screensaver_type = static_cast<models::ScreensaverType>(static_cast<int>(s.screensaver_type()));
    m.selected_display = to_display(s.selected_display());
    m.app_version = s.app_version();
    m.app_previous_version = s.app_previous_version();
    m.startup = s.startup();
    m.is_first_run = s.is_first_run();
    m.control_panel_opened = s.control_panel_opened();
    m.app_focus_pause = static_cast<models::AppRules>(static_cast<int>(s.app_focus_pause()));
    m.app_fullscreen_pause = static_cast<models::AppRules>(static_cast<int>(s.app_fullscreen_pause()));
    m.battery_pause = static_cast<models::AppRules>(static_cast<int>(s.battery_pause()));
    m.video_player = static_cast<models::LivelyMediaPlayer>(static_cast<int>(s.video_player()));
    m.video_player_hw_accel = s.video_player_hw_accel();
    m.web_browser = static_cast<models::LivelyWebBrowser>(static_cast<int>(s.web_browser()));
    m.gif_player = static_cast<models::LivelyGifPlayer>(static_cast<int>(s.gif_player()));
    m.picture_player = static_cast<models::LivelyPicturePlayer>(static_cast<int>(s.picture_player()));
    m.wallpaper_wait_time = s.wallpaper_wait_time();
    m.process_timer_interval = s.process_timer_interval();
    m.stream_quality = static_cast<models::StreamQualitySuggestion>(static_cast<int>(s.stream_quality()));
    m.lively_zip_generate = s.lively_zip_generate();
    m.scaler_video = static_cast<models::WallpaperScaler>(static_cast<int>(s.scaler_video()));
    m.scaler_gif = static_cast<models::WallpaperScaler>(static_cast<int>(s.scaler_gif()));
    m.gif_capture = s.gif_capture();
    m.multi_file_auto_import = s.multi_file_auto_import();
    m.safe_shutdown = s.safe_shutdown();
    m.is_restart = s.is_restart();
    m.input_forward = static_cast<models::InputForwardMode>(static_cast<int>(s.input_forward()));
    m.mouse_input_mov_always = s.mouse_input_mov_always();
    m.tile_size = s.tile_size();
    m.ui_mode = static_cast<models::LivelyGUIState>(static_cast<int>(s.lively_gui_rendering()));
    m.wallpaper_dir = s.wallpaper_dir();
    m.wallpaper_dir_move_existing_wallpaper_new_dir = s.wallpaper_dirmove_existing_wallpaper_new_dir();
    m.sys_tray_icon = s.sys_tray_icon();
    m.web_debug_port = s.web_debug_port();
    m.auto_detect_online_streams = s.auto_detect_online_streams();
    m.extract_stream_meta_data = s.extract_stream_meta_data();
    m.wallpaper_bundle_version = s.wallpaper_bundle_version();
    m.audio_volume_global = s.audio_volume_global();
    m.audio_only_on_desktop = s.audio_only_on_desktop();
    m.wallpaper_scaling = static_cast<models::WallpaperScaler>(static_cast<int>(s.wallpaper_scaling()));
    m.cef_disk_cache = s.cef_disk_cache();
    m.debug_menu = s.debug_menu();
    m.is_beta_opt_in = s.test_build();
    m.application_theme = static_cast<models::AppTheme>(static_cast<int>(s.application_theme()));
    m.remote_desktop_pause = static_cast<models::AppRules>(static_cast<int>(s.remote_desktop_pause()));
    m.power_save_mode_pause = static_cast<models::AppRules>(static_cast<int>(s.power_save_mode_pause()));
    m.lock_screen_auto_wallpaper = s.lock_screen_auto_wallpaper();
    m.desktop_auto_wallpaper = s.desktop_auto_wallpaper();
    m.system_taskbar_theme = static_cast<models::TaskbarTheme>(static_cast<int>(s.system_taskbar_theme()));
    // C# upstream quirk kept: ((ScreensaverIdleTime)((int)settings.ScreensaverIdleWait))
    // — proto positions (off_=0..min120=12) cast straight into the minutes-valued
    // C# enum. We preserve the resulting raw value (see header note).
    m.screensaver_idle_delay = static_cast<models::ScreensaverIdleTime>(static_cast<int>(s.screensaver_idle_wait()));
    m.screensaver_oled_warning = s.screensaver_oled_warning();
    m.screensaver_empty_screen_show_black = s.screensaver_empty_screen_show_black();
    m.screensaver_lock_on_resume = s.screensaver_lock_on_resume();
    m.language = s.language();
    m.keep_awake_ui = s.keep_awake_ui();
    m.display_pause_settings = static_cast<models::DisplayPause>(static_cast<int>(s.display_pause_settings()));
    m.remember_selected_screen = s.remember_selected_screen();
    m.is_updated = s.updated();
    m.is_updated_notify = s.updatednotify();
    m.application_theme_background = static_cast<models::AppThemeBackground>(static_cast<int>(s.application_theme_background()));
    if (!s.application_theme_background_path().empty()) {
        m.application_theme_background_path = s.application_theme_background_path();
    }
    m.theme_bundle_version = s.theme_bundle_version();
    m.is_screensaver_plugin_notify = s.screensaver_plugin_notify();
    m.screensaver_global_volume = s.screensaver_volume_global();
    m.screensaver_fade_in = s.screensaver_fade_in();
    m.video_target_color_space_mode = static_cast<models::TargetColorspaceHintMode>(static_cast<int>(s.video_target_color_space_mode()));
    m.visualizer_audio_device_id = s.visualizer_audio_device_id();
    m.display_audio_output = static_cast<models::DisplayAudioMode>(static_cast<int>(s.display_audio_output()));
    m.selected_audio_output_display = to_display(s.selected_audio_output_display());
    m.is_restart_after_lockscreen = s.restart_after_lockscreen();
    return m;
}

// C# CreateGrpcSettings — SettingsModel → proto (field-for-field, same skips).
SettingsDataModel to_proto(const models::SettingsModel& m) {
    SettingsDataModel s;
    s.set_saved_url(m.saved_url);
    s.set_process_monitor_alogorithm(
        static_cast<Lively::Grpc::Common::Proto::Settings::ProcessMonitorRule>(static_cast<int>(m.process_monitor_algorithm)));
    s.set_wallpaper_arrangement(
        static_cast<Lively::Grpc::Common::Proto::Settings::WallpaperArrangementRule>(static_cast<int>(m.wallpaper_arrangement)));
    s.set_screensaver_arrangement(
        static_cast<Lively::Grpc::Common::Proto::Settings::WallpaperArrangementRule>(static_cast<int>(m.screensaver_arragement)));
    s.set_screensaver_type(
        static_cast<Lively::Grpc::Common::Proto::Settings::ScreensaverTypeRule>(static_cast<int>(m.screensaver_type)));
    if (m.selected_display) {
        *s.mutable_selected_display() = from_display(*m.selected_display);
    }
    s.set_app_version(m.app_version);
    s.set_app_previous_version(m.app_previous_version);
    s.set_startup(m.startup);
    s.set_is_first_run(m.is_first_run);
    s.set_control_panel_opened(m.control_panel_opened);
    s.set_app_focus_pause(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(m.app_focus_pause)));
    s.set_app_fullscreen_pause(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(m.app_fullscreen_pause)));
    s.set_battery_pause(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(m.battery_pause)));
    s.set_video_player(static_cast<Lively::Grpc::Common::Proto::Settings::MediaPlayer>(static_cast<int>(m.video_player)));
    s.set_video_player_hw_accel(m.video_player_hw_accel);
    s.set_web_browser(static_cast<Lively::Grpc::Common::Proto::Settings::WebBrowser>(static_cast<int>(m.web_browser)));
    s.set_gif_player(static_cast<Lively::Grpc::Common::Proto::Settings::GifPlayer>(static_cast<int>(m.gif_player)));
    s.set_picture_player(static_cast<Lively::Grpc::Common::Proto::Settings::PicturePlayer>(static_cast<int>(m.picture_player)));
    s.set_wallpaper_wait_time(m.wallpaper_wait_time);
    s.set_process_timer_interval(m.process_timer_interval);
    s.set_stream_quality(static_cast<Lively::Grpc::Common::Proto::Settings::StreamQualitySuggestion>(static_cast<int>(m.stream_quality)));
    s.set_lively_zip_generate(m.lively_zip_generate);
    s.set_scaler_video(static_cast<Lively::Grpc::Common::Proto::Settings::WallpaperScalerRule>(static_cast<int>(m.scaler_video)));
    s.set_scaler_gif(static_cast<Lively::Grpc::Common::Proto::Settings::WallpaperScalerRule>(static_cast<int>(m.scaler_gif)));
    s.set_gif_capture(m.gif_capture);
    s.set_multi_file_auto_import(m.multi_file_auto_import);
    s.set_safe_shutdown(m.safe_shutdown);
    s.set_is_restart(m.is_restart);
    s.set_input_forward(static_cast<Lively::Grpc::Common::Proto::Settings::InputForwardMode>(static_cast<int>(m.input_forward)));
    s.set_mouse_input_mov_always(m.mouse_input_mov_always);
    s.set_tile_size(m.tile_size);
    s.set_lively_gui_rendering(static_cast<Lively::Grpc::Common::Proto::Settings::GuiMode>(static_cast<int>(m.ui_mode)));
    s.set_wallpaper_dir(m.wallpaper_dir);
    s.set_wallpaper_dirmove_existing_wallpaper_new_dir(m.wallpaper_dir_move_existing_wallpaper_new_dir);
    s.set_sys_tray_icon(m.sys_tray_icon);
    s.set_web_debug_port(m.web_debug_port);
    s.set_auto_detect_online_streams(m.auto_detect_online_streams);
    s.set_extract_stream_meta_data(m.extract_stream_meta_data);
    s.set_wallpaper_bundle_version(m.wallpaper_bundle_version);
    s.set_audio_volume_global(m.audio_volume_global);
    s.set_audio_only_on_desktop(m.audio_only_on_desktop);
    s.set_wallpaper_scaling(static_cast<Lively::Grpc::Common::Proto::Settings::WallpaperScalerRule>(static_cast<int>(m.wallpaper_scaling)));
    s.set_cef_disk_cache(m.cef_disk_cache);
    s.set_debug_menu(m.debug_menu);
    s.set_test_build(m.is_beta_opt_in);
    s.set_application_theme(static_cast<Lively::Grpc::Common::Proto::Settings::AppTheme>(static_cast<int>(m.application_theme)));
    s.set_remote_desktop_pause(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(m.remote_desktop_pause)));
    s.set_power_save_mode_pause(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(m.power_save_mode_pause)));
    s.set_lock_screen_auto_wallpaper(m.lock_screen_auto_wallpaper);
    s.set_desktop_auto_wallpaper(m.desktop_auto_wallpaper);
    s.set_system_taskbar_theme(static_cast<Lively::Grpc::Common::Proto::Settings::TaskbarTheme>(static_cast<int>(m.system_taskbar_theme)));
    // C# upstream quirk: ((uint)settings.ScreensaverIdleDelay) — minutes value
    // cast into the positional proto enum. Preserved verbatim.
    s.set_screensaver_idle_wait(static_cast<Lively::Grpc::Common::Proto::Settings::ScreensaverIdleTime>(
        static_cast<std::uint32_t>(static_cast<int>(m.screensaver_idle_delay))));
    s.set_screensaver_oled_warning(m.screensaver_oled_warning);
    s.set_screensaver_empty_screen_show_black(m.screensaver_empty_screen_show_black);
    s.set_screensaver_lock_on_resume(m.screensaver_lock_on_resume);
    s.set_language(m.language);
    s.set_keep_awake_ui(m.keep_awake_ui);
    s.set_display_pause_settings(static_cast<Lively::Grpc::Common::Proto::Settings::DisplayPauseRule>(static_cast<int>(m.display_pause_settings)));
    s.set_remember_selected_screen(m.remember_selected_screen);
    s.set_updated(m.is_updated);
    s.set_updatednotify(m.is_updated_notify);
    s.set_application_theme_background(static_cast<Lively::Grpc::Common::Proto::Settings::AppThemeBackground>(static_cast<int>(m.application_theme_background)));
    s.set_application_theme_background_path(m.application_theme_background_path.value_or(std::string()));
    s.set_theme_bundle_version(m.theme_bundle_version);
    s.set_screensaver_plugin_notify(m.is_screensaver_plugin_notify);
    s.set_screensaver_volume_global(m.screensaver_global_volume);
    s.set_screensaver_fade_in(m.screensaver_fade_in);
    s.set_video_target_color_space_mode(static_cast<Lively::Grpc::Common::Proto::Settings::TargetColorSpaceMode>(static_cast<int>(m.video_target_color_space_mode)));
    s.set_visualizer_audio_device_id(m.visualizer_audio_device_id);
    s.set_display_audio_output(static_cast<Lively::Grpc::Common::Proto::Settings::DisplayAudioMode>(static_cast<int>(m.display_audio_output)));
    if (m.selected_audio_output_display) {
        *s.mutable_selected_audio_output_display() = from_display(*m.selected_audio_output_display);
    }
    s.set_restart_after_lockscreen(m.is_restart_after_lockscreen);
    return s;
}

} // namespace

struct UserSettingsClient::Impl {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<SettingsService::Stub> stub;
};

UserSettingsClient::UserSettingsClient(const std::string& target)
    : impl_(std::make_unique<Impl>()) {
    impl_->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    impl_->stub = SettingsService::NewStub(impl_->channel);

    // C# ctor: Load<SettingsModel>() + Load<List<ApplicationRulesModel>>() —
    // both blocking, both before the constructor returns.
    load_settings();
    load_app_rules();
}

UserSettingsClient::~UserSettingsClient() = default;

void UserSettingsClient::load_settings() {
    grpc::ClientContext ctx;
    Empty req;
    SettingsDataModel resp;
    if (grpc::Status s = impl_->stub->GetSettings(&ctx, req, &resp); !s.ok()) throw rpc_error(s);
    settings_ = from_proto(resp);
}

void UserSettingsClient::load_app_rules() {
    grpc::ClientContext ctx;
    Empty req;
    AppRulesSettings resp;
    if (grpc::Status s = impl_->stub->GetAppRulesSettings(&ctx, req, &resp); !s.ok()) throw rpc_error(s);
    std::vector<ApplicationRulesModel> rules;
    for (const auto& item : resp.app_rules()) {
        rules.push_back({item.app_name(),
                         static_cast<models::AppRules>(static_cast<int>(item.rule()))});
    }
    app_rules_ = std::move(rules);
}

Task<> UserSettingsClient::load_settings_async() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<SettingsDataModel>();
    impl_->stub->async()->GetSettings(ctx.get(), req.get(), resp.get(),
        [this, tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) {
                settings_ = from_proto(*resp);
                tcs.set_result();
            } else {
                tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
            }
        });
    co_await tcs.task();
}

Task<> UserSettingsClient::load_app_rules_async() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<Empty>();
    auto resp = std::make_shared<AppRulesSettings>();
    impl_->stub->async()->GetAppRulesSettings(ctx.get(), req.get(), resp.get(),
        [this, tcs, ctx, req, resp](grpc::Status s) {
            if (!s.ok()) {
                tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
                return;
            }
            std::vector<ApplicationRulesModel> rules;
            for (const auto& item : resp->app_rules()) {
                rules.push_back({item.app_name(),
                                 static_cast<models::AppRules>(static_cast<int>(item.rule()))});
            }
            app_rules_ = std::move(rules);
            tcs.set_result();
        });
    co_await tcs.task();
}

void UserSettingsClient::save_settings() {
    grpc::ClientContext ctx;
    SettingsDataModel req = to_proto(settings_.value_or(models::SettingsModel{}));
    Empty resp;
    (void)impl_->stub->SetSettings(&ctx, req, &resp); // C# discards the result
}

void UserSettingsClient::save_app_rules() {
    grpc::ClientContext ctx;
    AppRulesSettings req;
    for (const auto& item : app_rules_) {
        auto* r = req.add_app_rules();
        r->set_app_name(item.app_name);
        r->set_rule(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(item.rule)));
    }
    Empty resp;
    (void)impl_->stub->SetAppRulesSettings(&ctx, req, &resp);
}

Task<> UserSettingsClient::save_settings_async() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<SettingsDataModel>(to_proto(settings_.value_or(models::SettingsModel{})));
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->SetSettings(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
        });
    co_await tcs.task();
}

Task<> UserSettingsClient::save_app_rules_async() {
    task_completion_source<> tcs;
    auto ctx = std::make_shared<grpc::ClientContext>();
    auto req = std::make_shared<AppRulesSettings>();
    for (const auto& item : app_rules_) {
        auto* r = req->add_app_rules();
        r->set_app_name(item.app_name);
        r->set_rule(static_cast<Lively::Grpc::Common::Proto::Settings::AppRules>(static_cast<int>(item.rule)));
    }
    auto resp = std::make_shared<Empty>();
    impl_->stub->async()->SetAppRulesSettings(ctx.get(), req.get(), resp.get(),
        [tcs, ctx, req, resp](grpc::Status s) {
            if (s.ok()) tcs.set_result();
            else tcs.set_exception(std::make_exception_ptr(rpc_error(s)));
        });
    co_await tcs.task();
}

} // namespace lively::rpc
