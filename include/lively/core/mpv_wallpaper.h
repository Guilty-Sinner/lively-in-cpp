#pragma once
// Port of Lively/Core/Wallpapers/VideoMpvPlayer.cs.
//
// The lifecycle, in the C#'s order, because the order is the behaviour:
//
//   ctor      build the option string and the IPC pipe name; nothing runs yet
//   show()    CreateProcess -> WaitForProcesWindow(20s) -> strip the window
//             chrome -> remove it from the taskbar -> push LivelyProperties ->
//             wait 69ms -> raise Loaded
//   close()   cancel the window wait, then send `{"command":["quit"]}` over IPC
//   terminate()  kill the process
//
// Three things are easy to get subtly wrong and are called out at the definitions
// instead of here: the window handle is not available until the child's message
// loop exists, so a wallpaper that fails to create a window must be killed rather
// than left orphaned; `close` and `terminate` are different operations (graceful
// IPC vs kill) even though IWallpaper.Dispose calls terminate; and the
// LivelyProperties push has to happen after the window exists but before Loaded,
// because the core treats Loaded as "the wallpaper is on screen and configured".

#include <lively/core/iwallpaper.h>
#include <lively/core/mpv.h>
#include <lively/core/win32/child_process.h>

#include <atomic>
#include <memory>
#include <string>

namespace lively::core {

class MpvWallpaper final : public IWallpaper {
public:
    // `lively_property_path` is the copy in Savedata/wpdata that the core chose
    // for this screen; empty means "no properties file".
    MpvWallpaper(std::string path,
                 models::LibraryModel model,
                 models::DisplayMonitor display,
                 std::string lively_property_path,
                 std::string app_base_directory,
                 bool is_hw_accel = true,
                 bool is_windowed = false,
                 models::TargetColorspaceHintMode color_space = models::TargetColorspaceHintMode::target,
                 models::StreamQualitySuggestion stream_quality = models::StreamQualitySuggestion::Highest);
    ~MpvWallpaper() override;

    MpvWallpaper(const MpvWallpaper&) = delete;
    MpvWallpaper& operator=(const MpvWallpaper&) = delete;

    bool is_exited() const override { return is_exited_.load(); }
    bool is_loaded() const override { return is_loaded_.load(); }
    models::WallpaperType category() const override { return model_.lively_info.type; }
    const models::LibraryModel& model() const override { return model_; }

    NativeHandle handle() const override;
    // The mpv window does not get raw input forwarded: VideoMpvPlayer returns
    // IntPtr.Zero for InputHandle, so the core's input forwarding is a no-op here.
    NativeHandle input_handle() const override { return nullptr; }
    int pid() const override;

    void show() override;
    void pause() override;
    void play() override;
    void close() override;
    void terminate() override;

    const models::DisplayMonitor& screen() const override { return screen_; }
    void set_screen(const models::DisplayMonitor& display) override { screen_ = display; }

    void send_message(const models::IpcMessage& message) override;
    const std::string& lively_property_copy_path() const override { return lively_property_path_; }

    void set_volume(int volume) override;
    void set_mute(bool mute) override;
    void set_playback_pos(float position, PlaybackPosType type) override;
    void screen_capture(const std::string& file_path) override;

    // The IPC pipe name (`"mpvsocket" + Path.GetRandomFileName()`).
    const std::string& ipc_server_name() const { return ipc_server_name_; }
    // The command line actually handed to mpv, for diagnostics and tests.
    const std::string& command_line() const { return command_line_; }
    // Mpv{uniqueId} log prefix: a per-process sequence number, as upstream.
    int unique_id() const { return unique_id_; }

private:
    // Sends an already-built IPC message; silently drops on a dead process, like
    // the C#'s `catch { }` around PipeClient.SendMessage.
    void send_ipc(const std::string& message);
    // VideoMpvPlayer.SetLivelyProperties — push every control's value to mpv.
    void apply_lively_properties();
    // VideoMpvPlayer.UpdateScaler — the property sequence for one scaler.
    void update_scaler(models::WallpaperScaler scaler);
    // GetMpvException for the last exit code.
    std::string exit_message() const;

    std::string file_path_;
    models::LibraryModel model_;
    models::DisplayMonitor screen_;
    std::string lively_property_path_;
    std::string app_base_directory_;
    bool is_hw_accel_ = true;
    bool is_windowed_ = false;
    models::TargetColorspaceHintMode color_space_ = models::TargetColorspaceHintMode::target;
    models::StreamQualitySuggestion stream_quality_ = models::StreamQualitySuggestion::Highest;

    std::string ipc_server_name_;
    std::string command_line_;
    int unique_id_ = 0;
    // 20,000 ticks = 20s, the C# `timeOut`.
    int window_wait_ticks_ = 20000;
    bool is_video_stopped_ = false;

    std::unique_ptr<win32::ChildProcess> process_;
    std::atomic<bool> is_loaded_{false};
    std::atomic<bool> is_exited_{false};
    // Set by close() before the graceful quit, polled by the window wait.
    std::atomic<bool> cancel_window_wait_{false};
    NativeHandle handle_ = nullptr;

    friend class MpvWallpaperTestAccess;
};

} // namespace lively::core
