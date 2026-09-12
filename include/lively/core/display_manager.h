#pragma once
// Port of Lively/Core/Display/DisplayManager.cs — the monitor enumeration the
// whole core is indexed by.
//
// Every layout rule, every screen-keyed lookup and every persisted layout entry
// resolves displays through this object, and the identity it hands out is not the
// Windows handle: `DisplayMonitor.Equals` compares **DeviceId only**, and DeviceId
// is normally the display's device interface path (from EnumDisplayDevices with
// EDD_GET_DEVICE_INTERFACE_NAME) — but for displays where Windows reports no
// device path it is a **synthetic id** derived from the monitor's bounds:
//
//     "\\?\DISPLAY#LOCALDISPLAY#" + lowercase-hex SHA-256("x-y-width-height")
//
// That fallback is why a wallpaper can be "restored" to a monitor that was
// unplugged and replugged into a different port: the synthetic id is a function of
// geometry, so a display that comes back at the same position and size is
// considered the same screen. It is also why moving a monitor changes the identity
// of a device-path-less display — a genuine C# behaviour, not an accident, and the
// reason this is spelled out here.
//
// The refresh logic is the part with a state machine in it:
//   * every existing monitor is marked `isStale` first,
//   * monitors found now are matched to existing entries **by DeviceName** (the
//     "\\.\DISPLAY1" style name) and reused, clearing the stale flag,
//   * whatever is still stale afterwards is removed.
// Reuse matters because the core holds `DisplayMonitor` values (in the running
// wallpaper list and the persisted layout) and compares them by DeviceId; building
// fresh objects every refresh would still compare equal by id, but the *stale*
// bookkeeping is what lets a display that vanished be noticed at all.
//
// `Index = i + 1` (one-based, in enumeration order) is part of the persisted
// layout JSON, so it is not cosmetic.

#include <lively/models/display_monitor.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lively::core {

class DisplayManager {
public:
    DisplayManager() = default;

    // RefreshDisplayMonitorList: re-enumerate, reuse by DeviceName, drop stale.
    void refresh();

    const std::vector<models::DisplayMonitor>& displays() const { return displays_; }

    // PrimaryDisplayMonitor: first with IsPrimary. Empty when none reports primary.
    std::optional<models::DisplayMonitor> primary_display() const;

    // VirtualScreenBounds: the union rectangle from the SM_*VIRTUALSCREEN metrics,
    // i.e. the whole desktop including monitors at negative coordinates.
    models::Rectangle virtual_screen_bounds() const;

    // IsMultiScreen(): DisplayMonitors.Count > 1 — nothing about geometry. A
    // single off-origin monitor is still single-screen, which is what the span
    // arrangement branch checks.
    bool is_multi_screen() const { return displays_.size() > 1; }

    // ScreenExists: any display with the same DeviceId.
    bool screen_exists(const models::DisplayMonitor& display) const;

    // GetDisplayMonitorFromHWnd: the monitor a window is on, by proximity. Used by
    // the preview window and the wallpaper error dialog.
    std::optional<models::DisplayMonitor> display_from_window(void* hwnd) const;
    // GetDisplayMonitorFromPoint.
    std::optional<models::DisplayMonitor> display_from_point(int x, int y) const;

private:
    // C# ObservableCollection<DisplayMonitor> — the list is mutated in place by
    // refresh() (reused entries, stale ones erased), so callers that hold an
    // iterator across a refresh would be holding a dangling one, exactly as in C#.
    std::vector<models::DisplayMonitor> displays_;
};

// DisplayManager.GetDefaultDisplayDeviceId, exposed because it is the identity
// rule above and is worth testing on its own.
std::string default_display_device_id(const models::Rectangle& bounds);

// GetSystemMetrics(SM_REMOTESESSION) != 0 — decides the id's prefix.
bool is_remote_session();

} // namespace lively::core
