#include <lively/core/mpv_wallpaper.h>

#include <lively/common/constants.h>
#include <lively/common/pipe_client.h>
#include <lively/models/lively_controls.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <random>
#include <regex>
#include <stdexcept>
#include <thread>

namespace lively::core {

namespace {

namespace fs = std::filesystem;

// Path.GetRandomFileName(): 8 characters from the conservative set .NET uses
// (lowercase letters, digits, a hyphen and underscore) plus ".tmp" — NOT a
// cryptographically random name, and the mpv pipe name inherits that shape.
std::string path_get_random_file_name() {
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789-_";
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(kChars) - 2);
    std::string name;
    name.reserve(12);
    for (int i = 0; i < 8; ++i)
        name += kChars[dist(rng)];
    return name + ".tmp";
}

// The per-process sequence number used only for log prefixes (Mpv{uniqueId}).
std::atomic<int> g_wallpaper_count{0};

// C#: Thread.Sleep(69) after applying properties — "Wait a bit for properties to
// apply. Todo: check ipc mgs and do this properly."
constexpr int kPropertyApplyDelayMs = 69;

} // namespace

MpvWallpaper::MpvWallpaper(std::string path,
                           models::LibraryModel model,
                           models::DisplayMonitor display,
                           std::string lively_property_path,
                           std::string app_base_directory,
                           bool is_hw_accel,
                           bool is_windowed,
                           models::TargetColorspaceHintMode color_space,
                           models::StreamQualitySuggestion stream_quality)
    : file_path_(std::move(path)),
      model_(std::move(model)),
      screen_(std::move(display)),
      lively_property_path_(std::move(lively_property_path)),
      app_base_directory_(std::move(app_base_directory)),
      is_hw_accel_(is_hw_accel),
      is_windowed_(is_windowed),
      color_space_(color_space),
      stream_quality_(stream_quality),
      ipc_server_name_("mpvsocket" + path_get_random_file_name()),
      unique_id_(g_wallpaper_count++) {
    MpvLaunchOptions options;
    options.type = model_.lively_info.type;
    options.path = file_path_;
    options.is_hw_accel = is_hw_accel_;
    options.is_windowed = is_windowed_;
    options.color_space = color_space_;
    options.stream_quality = stream_quality_;
    options.ipc_server_name = ipc_server_name_;
    options.config_dir = find_mpv_config_dir(app_base_directory_);
    command_line_ = build_mpv_command_line(options);
}

MpvWallpaper::~MpvWallpaper() {
    // IWallpaper : IDisposable, and VideoMpvPlayer.Dispose() calls Terminate.
    terminate();
}

NativeHandle MpvWallpaper::handle() const {
    return handle_;
}

int MpvWallpaper::pid() const {
    return process_ ? process_->pid() : kNoProcessId;
}

void MpvWallpaper::send_ipc(const std::string& message) {
    if (is_exited_.load())
        return;
    try {
        common::PipeClient::send_message(ipc_server_name_, message);
    } catch (...) {
        // C# swallows this: the pipe is gone when mpv has already died.
    }
}

void MpvWallpaper::apply_lively_properties() {
    if (lively_property_path_.empty())
        return;
    try {
        auto controls = models::LivelyPropertyUtil::GetControlsFromFile(lively_property_path_);
        for (const auto& [key, control] : controls) {
            (void)key;
            if (const auto* slider = dynamic_cast<const models::SliderModel*>(control.get())) {
                // Mpv is strongly typed; sending a decimal value for an integer
                // command fails. The rule lives in mpv_slider_command so the
                // banker's-rounding conversion is in one place.
                if (auto command = mpv_slider_command(slider->name, slider->value, slider->step))
                    send_ipc(*command);
            } else if (const auto* checkbox = dynamic_cast<const models::CheckboxModel*>(control.get())) {
                send_ipc(mpv_checkbox_command(checkbox->name, checkbox->value));
            } else if (const auto* scaler_dropdown =
                           dynamic_cast<const models::ScalerDropdownModel*>(control.get())) {
                // The dropdown's value is the Lively WallpaperScaler ordinal.
                update_scaler(static_cast<models::WallpaperScaler>(scaler_dropdown->value));
            }
        }
    } catch (...) {
        // C# logs and continues: a malformed properties file must not stop the
        // wallpaper from playing with mpv's defaults.
    }
}

void MpvWallpaper::update_scaler(models::WallpaperScaler scaler) {
    for (const auto& message : mpv_scaler_messages(scaler))
        send_ipc(message);
}

void MpvWallpaper::show() {
    if (!process_)
        process_ = std::make_unique<win32::ChildProcess>();

    const fs::path exe = fs::path(app_base_directory_) / "plugins" / "mpv" / "mpv.exe";
    const fs::path working_dir = fs::path(app_base_directory_) / "plugins" / "mpv";

    if (!process_->start(exe.string(), command_line_, working_dir.string())) {
        is_exited_.store(true);
        throw std::runtime_error(
            "Failed to start mpv. Expected the bundled player at " + exe.string() + ".");
    }

    try {
        // The window does not exist until mpv's message loop does. The C# waits
        // for input idle first and then polls for a visible window; polling alone
        // covers both, because a window cannot be found before the queue exists.
        bool cancelled = false;
        handle_ = win32::wait_for_process_window(process_->pid(), window_wait_ticks_, &cancelled,
                                                 reinterpret_cast<const volatile bool*>(
                                                     &cancel_window_wait_));
        if (cancelled) {
            // close() arrived while we were waiting: the wallpaper is being thrown
            // away, so the process must not be adopted onto the desktop.
            terminate();
            return;
        }
        if (handle_ == nullptr)
            throw std::runtime_error("Process window handle is null.");

        // Program ready. Strip the chrome and keep it out of Alt+Tab before it is
        // adopted — a titled window inside the desktop looks like a bug the user
        // cannot dismiss.
        win32::borderless_win_style(handle_);
        win32::remove_window_from_taskbar(handle_);

        // Restore this instance's LivelyProperties copy, then give mpv a moment to
        // apply them before the core is told the wallpaper is ready.
        apply_lively_properties();
        std::this_thread::sleep_for(std::chrono::milliseconds(kPropertyApplyDelayMs));

        is_loaded_.store(true);
        loaded.raise();
    } catch (...) {
        if (is_exited_.load()) {
            // The process died on its own: report *why* through mpv's exit code,
            // which is the difference between "your file is broken" and "Lively is
            // broken" in the UI.
            throw std::runtime_error(exit_message());
        }
        terminate();
        throw;
    }
}

void MpvWallpaper::pause() {
    send_ipc(mpv_set_property("pause", true));
}

void MpvWallpaper::play() {
    if (is_video_stopped_) {
        is_video_stopped_ = false;
        // "is this always the correct channel for main video?" — upstream comment.
        send_ipc(mpv_set_property("vid", 1));
    }
    send_ipc(mpv_set_property("pause", false));
}

void MpvWallpaper::close() {
    if (is_exited_.load())
        return;

    // C#: cancel the window wait and let it complete before sending quit, so a
    // quit cannot race the adoption of a window that is still appearing.
    cancel_window_wait_.store(true);
    while (process_ && process_->started() && handle_ == nullptr && !is_exited_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (process_->has_exited())
            break;
    }
    cancel_window_wait_.store(false);

    // Proc.CloseMainWindow() does not work for mpv — the graceful path is the IPC
    // quit command.
    send_ipc(mpv_ipc_command({"quit"}));
}

void MpvWallpaper::terminate() {
    if (is_exited_.load())
        return;
    if (process_) {
        process_->kill();
        is_exited_.store(true);
    }
}

void MpvWallpaper::set_volume(int volume) {
    send_ipc(mpv_set_property("volume", volume));
}

void MpvWallpaper::set_mute(bool mute) {
    // "We use mute as part of LivelyProperties, so disable track instead." — the
    // C# assumes the default audio track is 1.
    send_ipc(mpv_set_property("aid", mute ? "no" : "1"));
}

void MpvWallpaper::set_playback_pos(float position, PlaybackPosType type) {
    if (category() == models::WallpaperType::picture)
        return;
    const std::string mode = type == PlaybackPosType::absolutePercent ? "absolute-percent"
                                                                     : "relative-percent";
    send_ipc(mpv_seek(static_cast<double>(position), mode));
}

void MpvWallpaper::send_message(const models::IpcMessage& message) {
    using models::MessageType;
    switch (message.type()) {
        case MessageType::lp_slider: {
            const auto& slider = static_cast<const models::LivelySlider&>(message);
            if (auto command = mpv_slider_command(slider.name, slider.value, slider.step))
                send_ipc(*command);
            break;
        }
        case MessageType::lp_chekbox: {
            const auto& checkbox = static_cast<const models::LivelyCheckbox&>(message);
            send_ipc(mpv_checkbox_command(checkbox.name, checkbox.value));
            break;
        }
        case MessageType::lp_button: {
            const auto& button = static_cast<const models::LivelyButton&>(message);
            // A default button resets every control; a non-default one is unused
            // by mpv (upstream `else { }`).
            if (button.is_default)
                apply_lively_properties();
            break;
        }
        case MessageType::lp_dropdown_scaler: {
            const auto& scaler = static_cast<const models::LivelyDropdownScaler&>(message);
            update_scaler(static_cast<models::WallpaperScaler>(scaler.value));
            break;
        }
        // lp_dropdown / lp_textbox / lp_cpicker / lp_fdropdown are `//todo` in the
        // C# and so are not forwarded.
        default:
            break;
    }
}

void MpvWallpaper::screen_capture(const std::string& file_path) {
    // The gif branch uses ImageMagick to extract the first frame and rescale it
    // to at least 1080p with point filtering. This port has no image library, so
    // the branch is not implemented rather than approximated: a wrong thumbnail
    // is worse than a reported failure, and the caller (DesktopAutoWallpaper)
    // already treats a capture failure as non-fatal.
    if (category() == models::WallpaperType::gif)
        throw std::logic_error("Gif screen capture requires ImageMagick and is not ported.");

    if (!process_)
        throw std::runtime_error("ScreenCapture before ShowAsync.");

    send_ipc(mpv_ipc_command({"screenshot-to-file", file_path}));

    // The C# watches mpv's stdout for `Screenshot: '<path>'` and fails after 5
    // seconds. Polling the pipe in the caller's thread removes the C#'s need to
    // keep the read callback alive across the cancellation.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const std::regex pattern(R"(Screenshot: '([^']+)')");
    std::size_t scanned = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        process_->read_available_output();
        const std::string& output = process_->output_buffer();
        // Re-scan from the last position so an earlier line cannot match twice.
        auto begin = std::sregex_iterator(output.begin() + static_cast<std::ptrdiff_t>(scanned),
                                          output.end(), pattern);
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            const std::string reported = (*it)[1].str();
            if (reported.size() == file_path.size() &&
                std::equal(reported.begin(), reported.end(), file_path.begin(),
                           [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                      std::tolower(static_cast<unsigned char>(b));
                           })) {
                return;
            }
        }
        scanned = output.size();
        if (is_exited_.load())
            throw std::runtime_error("Process exited unexpectedly.");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Screenshot timed out.");
}

std::string MpvWallpaper::drain_output() {
    if (!process_)
        return std::string();
    return process_->read_available_output();
}

std::string MpvWallpaper::exit_message() const {
    if (!process_)
        return classify_mpv_exit(-1).message;
    return classify_mpv_exit(process_->exit_code()).message;
}

} // namespace lively::core
