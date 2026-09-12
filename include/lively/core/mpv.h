#pragma once
// Port of the mpv player host from Lively/Core/Wallpapers/VideoMpvPlayer.cs.
//
// The C# does NOT embed libmpv. It launches `plugins/mpv/mpv.exe` as a child
// process with a long option string, finds the child's window, adopts it onto
// the desktop, and then drives playback over mpv's JSON IPC channel — a Windows
// named pipe whose name it passes with `--input-ipc-server`. That is why this
// layer is portable without any media dependency at all: the "player" is a
// process plus a pipe, both of which already have ports here
// (lively::common::PipeClient).
//
// Two pieces are pure functions, so they are pinned against the real C# rather
// than reasoned about (tests/goldens/wallpaper_csharp.txt, from
// `csharp_probe wallpaper`):
//
//   * the command line — 21 fragments in a fixed order, including the quirks:
//     a stray single space where the gif case appends `--scale=nearest `,
//     `--no-border` vs `--border=yes` driven by `isWindowed`, the path wrapped
//     in literal quotes with no escaping (an embedded quote is passed through),
//     and `--config-dir="<dir>"` (also quoted) replaced wholesale by
//     `--no-config` when no portable config directory exists.
//   * the IPC JSON — `{"command":[...]}` terminated with `Environment.NewLine`,
//     i.e. CRLF. Newtonsoft renders a *float* as `1.5` but `-3.0`, and the
//     slider rule decides int-vs-double through Convert.ToInt32 (banker's
//     rounding), so both are load-bearing.
//
// The video-stream branch has a precedence quirk worth naming, because it is the
// kind of thing a "clean" port silently fixes. In C#,
//
//     link + qualitySuggestion switch { ... }
//
// parses as `link + (switch expression)`, so the URL is NOT quoted like the file
// paths are and the `--ytdl-format=...` option is glued onto the end of the same
// argument string. The fixture records the resulting bytes and the port
// reproduces them.

#include <lively/common/convert.h>
#include <lively/models/settings_model.h>   // WallpaperScaler, StreamQualitySuggestion, TargetColorspaceHintMode
#include <lively/models/wallpaper_type.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace lively::core {

// `--target-colorspace-hint-mode={target|source|source-dynamic}`.
// The C# throws ArgumentOutOfRangeException for anything else; the port has no
// out-of-range state to report, so it also has no `_` arm.
std::string mpv_target_colorspace(models::TargetColorspaceHintMode mode);

// The quality -> ytdl fragment table (`GetYtDlMpvArg`'s switch arm alone, without
// the leading URL). Highest is `bestvideo+bestaudio/best`; every other arm pins a
// height ceiling.
std::string mpv_ytdl_format(models::StreamQualitySuggestion quality);

// `GetMpvCommand`: JsonConvert.SerializeObject({"command": parts}) +
// Environment.NewLine. CRLF, because the pipe server is mpv.exe on Windows.
std::string mpv_ipc_command(const std::vector<nlohmann::json>& parts);

// The two command shapes the player sends, so call sites read like the C#:
//   GetMpvCommand("set_property", name, value)
//   GetMpvCommand("seek", pos, "absolute-percent")
std::string mpv_set_property(const std::string& name, const nlohmann::json& value);
std::string mpv_seek(double position, const std::string& mode);

// SetLivelyProperties' slider/checkbox arms. A slider whose Step is a whole
// number is sent as an int (Convert.ToInt32), otherwise as its double value.
// Returns nullopt when the C# would have thrown OverflowException.
std::optional<std::string> mpv_slider_command(const std::string& name, double value, double step);
std::string mpv_checkbox_command(const std::string& name, bool value);

// UpdateScaler: the property sequence that maps a Lively scaler onto mpv.
// Empty for `autofit` — the C# switch has no `auto` arm, so it sends nothing.
std::vector<std::string> mpv_scaler_messages(models::WallpaperScaler scaler);

// ---------------------------------------------------------------------------
// Launch

struct MpvLaunchOptions {
    models::WallpaperType type = models::WallpaperType::video;
    std::string path;                       // file path, or a URL for videostream
    bool is_hw_accel = true;
    bool is_windowed = false;
    models::TargetColorspaceHintMode color_space = models::TargetColorspaceHintMode::target;
    models::StreamQualitySuggestion stream_quality = models::StreamQualitySuggestion::Highest;
    // The IPC pipe name; the C# generates "mpvsocket" + Path.GetRandomFileName().
    // Passed in so the launcher is deterministic and testable.
    std::string ipc_server_name;
    // GetConfigDir(): the first existing directory of
    // {<base>/plugins/mpv/portable_config, <TempVideoDir>/portable_config}.
    // nullopt selects `--no-config`.
    std::optional<std::string> config_dir;
};

// The `cmdArgs` StringBuilder from VideoMpvPlayer's constructor, byte for byte.
std::string build_mpv_command_line(const MpvLaunchOptions& options);

// GetConfigDir(): the same discovery order, resolved against `base_dir` and the
// app's TempVideoDir. Split out from the command line so the layout is testable
// on a filesystem the test controls.
std::optional<std::string> find_mpv_config_dir(const std::string& base_dir);

// The mpv child's exit-code mapping (`GetMpvException`), so a launch failure
// surfaces as the C# exception type rather than a bare exit code.
struct MpvExitInfo {
    int exit_code = 0;
    std::string kind;    // "plugin" | "file" | "general"
    std::string message;
};

MpvExitInfo classify_mpv_exit(int exit_code);

} // namespace lively::core
