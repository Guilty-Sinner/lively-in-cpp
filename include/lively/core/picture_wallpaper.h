#pragma once
// Port of Lively/Core/Wallpapers/PictureWinAPI.cs.
//
// IDesktopWallpaper is a Windows 8 interface, and the MinGW-w64 headers gate its
// definition (not its forward declaration) behind `NTDDI_VERSION >= NTDDI_WIN8`.
// Without this the compiler sees only the forward declaration and reports
// "invalid use of incomplete type" at the first method call — so the API level is
// declared here rather than left to the toolchain default, and this header has to
// precede every other Windows include in the translation unit.
#ifdef _WIN32
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000   // NTDDI_WIN8
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#endif
//
// This is the one wallpaper kind with no window and no process: a still image is
// handed to Windows itself through IDesktopWallpaper, so the "wallpaper" is the
// OS desktop background. That is why every window-shaped member is a no-op or
// zero here (Handle => IntPtr.Zero, Pid => null) and why ShowAsync is the whole
// implementation.
//
// Two details the C# records as source comments are load-bearing and kept:
//
//   * Lively's `WallpaperScaler` is NOT Windows' scaler. `fill` maps to
//     Stretch, `uniform` to Fit, and `uniformFill` to Fill with a note that the
//     pivots differ (Lively's uniform-fill pivots top-left, Windows centres).
//     `auto` maps to Fill with a `todo` beside it. The mapping is pinned by
//     tests/goldens/wallpaper_csharp.txt because swapping Stretch and Fill is
//     visually plausible and silently wrong.
//   * `RestoreWallpaper` is COMMENTED OUT upstream. The constructor still reads
//     every monitor's current wallpaper into `wallpapersToRestore`, and Close
//     still calls a method whose body is empty — so closing a picture wallpaper
//     does NOT bring back the user's previous background. The port keeps both
//     halves: the bookkeeping (so the same failure modes occur) and the empty
//     restore, with the reason recorded rather than "fixed", because restoring
//     is a behaviour change the user would notice.
//
// The scaler position and the span rule are the only parts that are pure, so
// they are the only parts pinned against the real C#. The COM calls themselves
// are the port's own translation of the same interface methods.

#include <lively/core/iwallpaper.h>
#include <lively/models/settings_model.h>   // WallpaperScaler, WallpaperArrangement

#include <optional>
#include <string>
#include <vector>

namespace lively::core {

// DESKTOP_WALLPAPER_POSITION, from shobjidl.h (DWPOS_CENTER = 0 .. DWPOS_SPAN = 5).
enum class DesktopWallpaperPosition : int {
    Center = 0,
    Tile = 1,
    Stretch = 2,
    Fit = 3,
    Fill = 4,
    Span = 5,
};

// PictureWinApi's `desktopScaler` switch, plus the Show() rule that the span
// arrangement overrides it with Span. Total: every WallpaperScaler maps.
DesktopWallpaperPosition picture_scaler_position(models::WallpaperScaler scaler);
DesktopWallpaperPosition picture_show_position(models::WallpaperScaler scaler,
                                               models::WallpaperArrangement arrangement);

// One monitor's current background, captured before it is replaced.
struct SystemWallpaper {
    std::string device_id;
    std::string file_path;
};

class PictureWallpaper final : public IWallpaper {
public:
    PictureWallpaper(std::string file_path,
                     models::LibraryModel model,
                     models::DisplayMonitor display,
                     models::WallpaperArrangement arrangement,
                     models::WallpaperScaler scaler = models::WallpaperScaler::fill);
    ~PictureWallpaper() override;

    PictureWallpaper(const PictureWallpaper&) = delete;
    PictureWallpaper& operator=(const PictureWallpaper&) = delete;

    bool is_exited() const override { return is_exited_; }
    // IsLoaded => true: there is nothing to load.
    bool is_loaded() const override { return true; }
    models::WallpaperType category() const override { return models::WallpaperType::picture; }
    const models::LibraryModel& model() const override { return model_; }
    NativeHandle handle() const override;
    NativeHandle input_handle() const override;
    int pid() const override { return kNoProcessId; }

    void show() override;
    // Pause/Play/set_volume/set_mute/set_playback_pos/send_message are all
    // documented no-ops upstream.
    void pause() override {}
    void play() override {}
    void close() override;
    void terminate() override { close(); }

    const models::DisplayMonitor& screen() const override { return screen_; }
    void set_screen(const models::DisplayMonitor& display) override { screen_ = display; }

    void send_message(const models::IpcMessage&) override {}
    const std::string& lively_property_copy_path() const override;

    void set_volume(int) override {}
    void set_mute(bool) override {}
    void set_playback_pos(float, PlaybackPosType) override {}

    // C# throws NotImplementedException. Kept as a throw rather than a silent
    // no-op: the core's screenshot path relies on it (DesktopAutoWallpaper and
    // the wallpaper-tinted taskbar), so a picture wallpaper with auto-wallpaper
    // enabled must fail loudly in the same place the C# does.
    void screen_capture(const std::string& file_path) override;

    const std::vector<SystemWallpaper>& wallpapers_to_restore() const {
        return wallpapers_to_restore_;
    }

private:
    std::string file_path_;
    models::LibraryModel model_;
    models::DisplayMonitor screen_;
    models::WallpaperArrangement arrangement_;
    models::WallpaperScaler scaler_;
    std::vector<SystemWallpaper> wallpapers_to_restore_;
    bool is_exited_ = false;
    std::string empty_property_path_;
};

} // namespace lively::core
