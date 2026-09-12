#include <lively/core/mpv.h>

#include <lively/common/constants.h>
#include <lively/common/file_util.h>

#include <stdexcept>
#include <string>

namespace lively::core {

namespace {

namespace fs = std::filesystem;

// Every fragment below is copied from VideoMpvPlayer's constructor in order.
// The trailing spaces are part of the contract, not formatting: the C# appends
// each option with its own trailing space, so a build that "tidies" them changes
// the argument string mpv parses.
constexpr const char* kVolume = "--volume=0 ";
constexpr const char* kMsgLevel = "--msg-level=all=info ";
constexpr const char* kLoopFile = "--loop-file ";
constexpr const char* kKeepOpen = "--keep-open ";
constexpr const char* kMediaControls = "--media-controls=no ";
constexpr const char* kGeometry = "--geometry=-9999:0 ";
constexpr const char* kForceWindow = "--force-window=yes ";
constexpr const char* kNoWindowDragging = "--no-window-dragging ";
constexpr const char* kCursorAutohide = "--cursor-autohide=no ";
constexpr const char* kWindowMinimized = "--window-minimized=yes ";
constexpr const char* kStopScreensaver = "--stop-screensaver=no ";
constexpr const char* kInputDefaultBindings = "--input-default-bindings=no ";
constexpr const char* kNoBorder = "--no-border ";
constexpr const char* kBorder = "--border=yes ";
constexpr const char* kInputCursor = "--input-cursor=no ";
constexpr const char* kNoOsc = "--no-osc ";
constexpr const char* kScaleNearest = "--scale=nearest ";
constexpr const char* kHwdecAutoSafe = "--hwdec=auto-safe ";
constexpr const char* kHwdecNo = "--hwdec=no ";
constexpr const char* kNoConfig = "--no-config ";

} // namespace

std::string mpv_target_colorspace(models::TargetColorspaceHintMode mode) {
    switch (mode) {
        case models::TargetColorspaceHintMode::target: return "target";
        case models::TargetColorspaceHintMode::source: return "source";
        case models::TargetColorspaceHintMode::sourceDynamic: return "source-dynamic";
    }
    // Unreachable for a valid enum, matching the C#'s exhaustive switch.
    throw std::invalid_argument("Unsupported color space target");
}

std::string mpv_ytdl_format(models::StreamQualitySuggestion quality) {
    // Note the leading space in every arm: the C# returns the option *with* its
    // separator because it is concatenated straight onto the URL.
    switch (quality) {
        case models::StreamQualitySuggestion::Lowest:
            return " --ytdl-format=bestvideo[height<=144]+bestaudio/best";
        case models::StreamQualitySuggestion::Low:
            return " --ytdl-format=bestvideo[height<=240]+bestaudio/best";
        case models::StreamQualitySuggestion::LowMedium:
            return " --ytdl-format=bestvideo[height<=360]+bestaudio/best";
        case models::StreamQualitySuggestion::Medium:
            return " --ytdl-format=bestvideo[height<=480]+bestaudio/best";
        case models::StreamQualitySuggestion::MediumHigh:
            return " --ytdl-format=bestvideo[height<=720]+bestaudio/best";
        case models::StreamQualitySuggestion::High:
            return " --ytdl-format=bestvideo[height<=1080]+bestaudio/best";
        case models::StreamQualitySuggestion::Highest:
            return " --ytdl-format=bestvideo+bestaudio/best";
    }
    return std::string();
}

std::string mpv_ipc_command(const std::vector<nlohmann::json>& parts) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& part : parts)
        array.push_back(part);
    nlohmann::json wrapper;
    wrapper["command"] = std::move(array);
    // Environment.NewLine on Windows — mpv's IPC reader is line oriented and the
    // C# writes "\"command\":[...]}\r\n".
    return wrapper.dump() + "\r\n";
}

std::string mpv_set_property(const std::string& name, const nlohmann::json& value) {
    return mpv_ipc_command({"set_property", name, value});
}

std::string mpv_seek(double position, const std::string& mode) {
    return mpv_ipc_command({"seek", position, mode});
}

std::optional<std::string> mpv_slider_command(const std::string& name, double value, double step) {
    if (common::slider_step_is_fraction(step))
        return mpv_set_property(name, nlohmann::json(value));
    auto rounded = common::convert_to_int32(value);
    if (!rounded)
        return std::nullopt;   // C#: OverflowException, caught and logged
    return mpv_set_property(name, nlohmann::json(*rounded));
}

std::string mpv_checkbox_command(const std::string& name, bool value) {
    return mpv_set_property(name, nlohmann::json(value));
}

std::vector<std::string> mpv_scaler_messages(models::WallpaperScaler scaler) {
    // VideoMpvPlayer.UpdateScaler. The values are strings ("yes"/"no"/"0.0"),
    // not booleans — mpv accepts both, but only the string form matches the wire.
    switch (scaler) {
        case models::WallpaperScaler::none:
            return {mpv_set_property("keepaspect", "yes"),
                    mpv_set_property("video-unscaled", "yes")};
        case models::WallpaperScaler::fill:
            return {mpv_set_property("video-unscaled", "no"),
                    mpv_set_property("keepaspect", "no")};
        case models::WallpaperScaler::uniform:
            return {mpv_set_property("panscan", "0.0"),
                    mpv_set_property("video-unscaled", "no"),
                    mpv_set_property("keepaspect", "yes")};
        case models::WallpaperScaler::uniformFill:
            return {mpv_set_property("video-unscaled", "no"),
                    mpv_set_property("keepaspect", "yes"),
                    mpv_set_property("panscan", "1.0")};
        case models::WallpaperScaler::autofit:
            // C# `auto` has no arm in the switch: it sends nothing at all, so the
            // wallpaper keeps mpv's defaults rather than being reset.
            return {};
    }
    return {};
}

std::string build_mpv_command_line(const MpvLaunchOptions& options) {
    std::string cmd;
    cmd.reserve(512);
    cmd += kVolume;
    cmd += kMsgLevel;
    cmd += kLoopFile;
    cmd += kKeepOpen;
    cmd += kMediaControls;
    cmd += kGeometry;
    cmd += kForceWindow;
    cmd += kNoWindowDragging;
    cmd += kCursorAutohide;
    cmd += kWindowMinimized;
    cmd += kStopScreensaver;
    cmd += kInputDefaultBindings;
    cmd += options.is_windowed ? kBorder : kNoBorder;
    cmd += kInputCursor;
    cmd += kNoOsc;
    cmd += "--input-ipc-server=" + options.ipc_server_name + " ";
    // The gif arm appends its own fragment; every other type appends a lone space.
    // That extra space is why the gif and non-gif command lines differ by more
    // than the option itself, and it is in the fixture.
    cmd += options.type == models::WallpaperType::gif ? kScaleNearest : " ";
    cmd += options.is_hw_accel ? kHwdecAutoSafe : kHwdecNo;
    cmd += "--target-colorspace-hint-mode=" + mpv_target_colorspace(options.color_space) + " ";
    if (options.config_dir)
        cmd += "--config-dir=\"" + *options.config_dir + "\" ";
    else
        cmd += kNoConfig;
    // Local files are wrapped in literal quotes; a video stream is not (see the
    // header's note on the C# precedence). Neither is escaped.
    if (options.type == models::WallpaperType::videostream)
        cmd += options.path + mpv_ytdl_format(options.stream_quality);
    else
        cmd += "\"" + options.path + "\"";
    return cmd;
}

std::optional<std::string> find_mpv_config_dir(const std::string& base_dir) {
    // GetConfigDir(): first existing directory wins, in this order.
    const std::vector<std::string> dirs = {
        (fs::path(base_dir) / "plugins" / "mpv" / "portable_config").string(),
        (fs::path(common::TempVideoDirNarrow()) / "portable_config").string(),
    };
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            return dir;
    }
    return std::nullopt;
}

MpvExitInfo classify_mpv_exit(int exit_code) {
    // GetMpvException: mpv's documented exit codes, mapped onto the typed
    // exceptions the core reports through the RPC layer.
    MpvExitInfo info;
    info.exit_code = exit_code;
    switch (exit_code) {
        case 1:
            info.kind = "plugin";
            info.message = "Error initializing mpv. This is also returned if unknown options are passed to mpv.";
            break;
        case 2:
        case 3:
            info.kind = "file";
            info.message = "The file passed to mpv couldn't be played.";
            break;
        default:
            // Properties.Resources.LivelyExceptionGeneral, verbatim (including its
            // embedded newline and trailing text) — it is shown to the user in the
            // UI's error dialog, so it is part of the behaviour, not a log line.
            info.kind = "general";
            info.message =
                "Oops... Looks like something went wrong :(\n"
                "In order to better understand this error and fix the issue, share the log file with the developer.\n";
            break;
    }
    return info;
}

} // namespace lively::core
