#pragma once
// Port of Lively.Grpc.Client/IDesktopCoreClient + WinDesktopCoreClient.
//
// C# surface (IDesktopCoreClient):
//   Wallpapers / BaseDirectory / AssemblyVersion / IsCoreInitialized
//   Task CloseAllWallpapers(); CloseWallpaper(monitor|item|type);
//   Task SetWallpaper(path, monitorId);
//   Task<bool> EditWallpaper(path); Task<string> CreateWallpaper(file, type, args);
//   void SendMessageWallpaper(...); Task PreviewWallpaper(path);
//   Task TakeScreenshot(monitorId, savePath);
//   event EventHandler WallpaperChanged;  event EventHandler<Exception> WallpaperError;
//
// C# construction behaviour preserved: the constructor blocks until the
// initial GetWallpapers + GetCoreStats round-trips complete, then starts the
// two server-streaming subscription loops (WallpaperChanged / WallpaperError)
// on background threads, cancellable via Dispose → CancellationTokenSource.
//
// The C# `List<WallpaperData>` → `ReadOnlyCollection` wrapper maps to a
// mutex-guarded vector handed out by value (an immutable snapshot).

#include <lively/events.h>
#include <lively/models/display_monitor.h>
#include <lively/models/ipc_message.h>
#include <lively/models/wallpaper_type.h>
#include <lively/task.h>

#include <memory>
#include <string>
#include <vector>

namespace lively::rpc {

// IDesktopCoreClient.WallpaperData
struct WallpaperData {
    std::string lively_info_folder_path; // C# LivelyInfoFolderPath
    std::string lively_property_copy_path;
    std::string thumbnail_path;
    std::string preview_path;
    models::WallpaperType category = models::WallpaperType::app;
    models::DisplayMonitor display;
};

class DesktopCoreClient {
public:
    explicit DesktopCoreClient(const std::string& target);
    ~DesktopCoreClient(); // RAII replacement for C# IDisposable (cancel + join)

    DesktopCoreClient(const DesktopCoreClient&) = delete;
    DesktopCoreClient& operator=(const DesktopCoreClient&) = delete;

    // C# event EventHandler WallpaperChanged / EventHandler<Exception> WallpaperError.
    // EventHandler maps to event<TArgs>; EventArgs.Empty → const unit& payload.
    struct Unit {};
    event<Unit> wallpaper_changed;
    event<std::exception_ptr> wallpaper_error;

    // IDesktopCoreClient members (C# property → accessor).
    std::vector<WallpaperData> wallpapers() const; // snapshot of Wallpapers
    std::string base_directory() const { return base_directory_; }
    std::string assembly_version() const { return assembly_version_; }
    bool is_core_initialized() const { return is_core_initialized_; }

    // Requests — same signatures as the C# interface, minus the C# model
    // wrappers (LibraryModel/DisplayMonitor overloads take the raw ids).
    Task<> set_wallpaper(const std::string& lively_info_path, const std::string& monitor_id);
    Task<bool> edit_wallpaper(const std::string& lively_info_path);
    Task<std::string> create_wallpaper(const std::string& file_path,
                                       models::WallpaperType type,
                                       const std::string& arguments = {});
    Task<> close_all_wallpapers();
    Task<> close_wallpaper(const std::string& monitor_id);
    Task<> close_wallpaper_library(const std::string& lively_info_path);
    Task<> close_wallpaper_category(models::WallpaperType type);
    Task<> preview_wallpaper(const std::string& lively_info_path);
    Task<> take_screenshot(const std::string& monitor_id, const std::string& save_path);

    // C# SendMessageWallpaper(LibraryModel, IpcMessage) / (DisplayMonitor, ...) —
    // msg is serialized with the ported Newtonsoft-identical IPC serializer
    // (C# uses JsonUtil.Serialize(msg) — JsonConvert.SerializeObject under the hood).
    void send_message_wallpaper(const std::string& monitor_id,
                                const std::string& lively_info_path,
                                const models::IpcMessage& msg);

    // Exceptions raised by the two subscriptions (C# routes them through
    // WallpaperError with GetException(error) mappings below).
    // ErrorCategory → typed exception, exactly per WinDesktopCoreClient.GetException.
    static std::exception_ptr map_error(int error_category, const std::string& error_msg);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void subscribe_wallpaper_changed_loop();
    void subscribe_wallpaper_error_loop();

    std::string target_;
    std::string base_directory_;
    std::string assembly_version_;
    bool is_core_initialized_ = false;
};

} // namespace lively::rpc
