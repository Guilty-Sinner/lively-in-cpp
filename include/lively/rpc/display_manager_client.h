#pragma once
// Port of Lively.Grpc.Client/IDisplayManagerClient + DisplayManagerClient.
//
// C# surface (IDisplayManagerClient):
//   ReadOnlyCollection<DisplayMonitor> DisplayMonitors { get; }
//   DisplayMonitor PrimaryMonitor { get; }             // FirstOrDefault(IsPrimary)
//   Rectangle VirtulScreenBounds { get; }              // [sic] C# property name
//   event EventHandler DisplayChanged;
//
// Constructor blocks until the initial GetScreens + GetVirtualScreenBounds
// round-trips finish (C# Task.Run(...).Wait()), then starts the
// SubscribeDisplayChanged stream loop on a background thread; the handler
// re-fetches both snapshots and raises DisplayChanged (same as C#).

#include <lively/events.h>
#include <lively/models/display_monitor.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lively::rpc {

class DisplayManagerClient {
public:
    explicit DisplayManagerClient(const std::string& target);
    ~DisplayManagerClient(); // RAII: cancel + join the subscription thread

    DisplayManagerClient(const DisplayManagerClient&) = delete;
    DisplayManagerClient& operator=(const DisplayManagerClient&) = delete;

    struct Unit {};
    event<Unit> display_changed;

    std::vector<models::DisplayMonitor> display_monitors() const; // snapshot
    std::optional<models::DisplayMonitor> primary_monitor() const;
    models::Rectangle virtual_screen_bounds() const { return virtual_screen_bounds_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void subscribe_display_changed_loop();

    std::string target_;
    models::Rectangle virtual_screen_bounds_;
};

} // namespace lively::rpc
