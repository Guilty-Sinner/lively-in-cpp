#pragma once
// Port of Lively/Core/IWallpaper.cs — the contract every wallpaper kind
// implements, and the surface the RPC layer and the desktop placeholder
// delegate onto.
//
// The C# is `IDisposable`; here the destructor is the disposal (RAII, per the
// translation map in the README), with `close()`/`terminate()` kept as the
// explicit operations the core calls in a specific order during replacement and
// shutdown. Note the C# `Dispose` is NOT the same as `Close`: every
// implementation's Dispose calls Terminate, and for mpv Terminate is Kill while
// Close is a graceful IPC `quit`. Collapsing them would turn every wallpaper
// replacement into a process kill.
//
// `SendMessage(IpcMessage)` stays in the interface even though the picture
// wallpaper ignores it: the core broadcasts volume/mute/playback to whatever is
// running without knowing the kind, so a port that dropped the method would move
// that knowledge into the caller and drift from the C# control flow.

#include <lively/events.h>
#include <lively/models/display_monitor.h>
#include <lively/models/ipc_message.h>
#include <lively/models/library_model.h>
#include <lively/models/wallpaper_type.h>

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lively::core {

#ifdef _WIN32
using NativeHandle = HWND;
#else
using NativeHandle = void*;
#endif

// IWallpaper.cs's nested enum.
enum class PlaybackPosType : int { absolutePercent = 0, relativePercent };

// The subset of `event EventHandler` this interface exposes. The port's
// `lively::event<T>` (include/lively/events.h) carries the sender for the
// general case; here both handlers are parameterless in practice — the C#
// callbacks only ever read `sender as IWallpaper` from their own closure — so
// the interface exposes the raise side and the core subscribes by capturing the
// wallpaper it created, exactly as `wallpaper.Loaded += Wallpaper_Loaded` does.
//
// Deliberately NOT `lively::event<...>`: a wallpaper's handlers are wired up by
// the core before ShowAsync and never multicast, and the C# declares them as
// plain events on the interface rather than a shared shaped type.

class IWallpaper {
public:
    virtual ~IWallpaper() = default;

    // Wallpaper exit event fired.
    virtual bool is_exited() const = 0;
    // Wallpaper loading complete (includes LivelyProperties restoration).
    virtual bool is_loaded() const = 0;
    virtual models::WallpaperType category() const = 0;
    virtual const models::LibraryModel& model() const = 0;

    // Get window handle. Zero for wallpapers that are not windows (picture).
    virtual NativeHandle handle() const = 0;
    // Handle to the input window (the window raw input is forwarded to).
    virtual NativeHandle input_handle() const = 0;
    // Process id, or -1 for "not a program wallpaper" (C# `int? Pid`).
    virtual int pid() const = 0;

    virtual void show() = 0;
    virtual void pause() = 0;
    virtual void play() = 0;
    // Close wallpaper gracefully.
    virtual void close() = 0;
    // Immediately kill. Only meaningful for program wallpapers; otherwise the
    // same as close().
    virtual void terminate() = 0;

    // Display device the wallpaper is running on.
    virtual const models::DisplayMonitor& screen() const = 0;
    virtual void set_screen(const models::DisplayMonitor& display) = 0;

    // Send an IPC message to a program wallpaper.
    virtual void send_message(const models::IpcMessage& message) = 0;

    // Location of this instance's LivelyProperties.json copy under
    // SaveData/wpdata — a copy, because different screens get different copies.
    // Empty when there is no file.
    virtual const std::string& lively_property_copy_path() const = 0;

    // Volume 0-100.
    virtual void set_volume(int volume) = 0;
    virtual void set_mute(bool mute) = 0;
    // Timeline position; only 0 is meaningful for non-video wallpapers.
    virtual void set_playback_pos(float position, PlaybackPosType type) = 0;
    // Capture the wallpaper view to a .jpg.
    virtual void screen_capture(const std::string& file_path) = 0;

    // Raised by the implementation; the core wires these before show().
    lively::event<void> exited;
    lively::event<void> loaded;
};

// C# `int? Pid` for a wallpaper that has no process.
inline constexpr int kNoProcessId = -1;

} // namespace lively::core
