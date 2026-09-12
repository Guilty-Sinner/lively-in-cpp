#pragma once
// Port of the *decision* half of Lively/Core/WinDesktopCore.cs — the rules that
// decide which wallpaper belongs on which screen when displays come and go.
//
// Why this exists as a separate, Win32-free unit: in the C# the logic is
// entangled with `WorkerW` parenting, `SetWindowPos` and the wallpaper factory,
// which makes it untestable without a desktop. The rules themselves, though, are
// pure set operations over (wallpapers, displays, arrangement), and the C# is
// entirely explicit about them. Pulling them out is what makes them verifiable —
// and the reconciliation rules are where a port silently diverges: unplug a
// monitor on the ported build and you get a different wallpaper than the C# app.
//
// Everything here mirrors a specific C# expression, named in the comments. The
// `[desktop-oracle]` fixture drives the same scenarios through the real LINQ and
// compares the resulting plans line for line. What is NOT covered by tests is the
// part the C# does after each decision (WorkerW parenting, window geometry,
// process lifecycle) — see the README's status table.
//
// Three quirks that the port must not "fix":
//
//   1. `DisplayMonitor.Equals` compares **DeviceId only**, so every comparison
//      here is device-id equality; two monitors with different bounds but the
//      same id are "the same screen".
//   2. In `per` arrangement the orphan list is appended to `wallpapersDisconnected`
//      only when that display is not already queued — deduped by DeviceId, but the
//      *stored* entry is a fresh copy of the orphan's screen.
//   3. `duplicate` does NOT queue disconnected wallpapers at all: the arrangement
//      assumes every screen shows the same wallpaper, so it is re-created rather
//      than remembered.

#include <lively/models/display_monitor.h>
#include <lively/models/settings_model.h>            // WallpaperArrangement
#include <lively/models/wallpaper_layout.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lively::core {

// One running wallpaper: the display it is parented to plus the library item it
// was created from (C# `IWallpaper.Screen` / `.Model.LivelyInfoFolderPath`).
struct WallpaperSlot {
    models::DisplayMonitor screen;
    std::string lively_info_path;

    // C# `wallpaper.Screen.Equals(display)`.
    bool on(const models::DisplayMonitor& display) const {
        return models::same_display(screen, display);
    }
};

// True when a wallpaper's screen is no longer present in `displays` —
// C# `allScreens.Find(screen => wallpaper.Screen.Equals(screen)) == null`.
bool is_orphan(const WallpaperSlot& wallpaper, const std::vector<models::DisplayMonitor>& displays);

// displayManager.IsMultiScreen(): more than one monitor, or one that is not the
// virtual-screen origin. (C# compares against the primary display's bounds.)
bool is_multi_screen(const std::vector<models::DisplayMonitor>& displays);

// union of all displays' bounds == virtual screen bounds (C# VirtualScreenBounds).
models::Rectangle virtual_screen_bounds(const std::vector<models::DisplayMonitor>& displays);

// ---------------------------------------------------------------------------
// RefreshWallpaper() — a display was added/removed/changed.

struct RefreshPlan {
    // Orphans, in `wallpapers` order.
    std::vector<WallpaperSlot> orphans;
    // Only populated for `per`: entries appended to `wallpapersDisconnected`.
    std::vector<models::WallpaperLayoutModel> disconnected_added;
    // The value `Settings.SelectedDisplay` is set to (the old one if it survived,
    // else the primary).
    models::DisplayMonitor selected_display;
    bool refresh_desktop = true;
};

RefreshPlan plan_refresh(const std::vector<WallpaperSlot>& wallpapers,
                         const std::vector<models::DisplayMonitor>& displays,
                         const std::vector<models::WallpaperLayoutModel>& disconnected,
                         models::WallpaperArrangement arrangement,
                         const models::DisplayMonitor& selected_display,
                         const models::DisplayMonitor& primary);

// ---------------------------------------------------------------------------
// UpdateWallpaperRect() — desktop size changed.

struct RectUpdate {
    std::size_t index = 0;               // index into `wallpapers`
    models::DisplayMonitor screen;       // the (live) display to rebind to
    models::Rectangle rect;              // target window rect
    int z_order = 1;                     // C# SetWindowPos hWndInsertAfter
};

struct RectPlan {
    bool span = false;                   // the span branch (single window)
    // Span: rebind wallpapers[0] to the primary display over the whole virtual
    // screen. Exactly one entry, or empty when there is nothing to update.
    std::optional<RectUpdate> span_update;
    // Otherwise: one entry per display, in `displays` order, that has a wallpaper.
    std::vector<RectUpdate> screen_updates;
    bool refresh_desktop = true;
};

// `wallpapers` is only read for the span branch (it needs index 0) and for the
// FindIndex in the per-screen branch.
RectPlan plan_update_rect(const std::vector<WallpaperSlot>& wallpapers,
                          const std::vector<models::DisplayMonitor>& displays,
                          models::WallpaperArrangement arrangement);

// ---------------------------------------------------------------------------
// RestoreDisconnectedWallpapers() — a screen came back.

struct RestorePlan {
    // Entries to feed to plan_restore(), in `disconnected` order (per), or the
    // single wallpaper to re-create on a new screen (duplicate).
    std::vector<models::WallpaperLayoutModel> to_restore;
    // duplicate only: the display that has no wallpaper yet, if any.
    std::optional<models::DisplayMonitor> duplicate_screen;
    // Which existing wallpaper is re-created for duplicate_screen
    // (C# `Wallpapers[0].Model`) — index into `wallpapers`.
    std::size_t duplicate_source_index = 0;
};

RestorePlan plan_restore_disconnected(const std::vector<WallpaperSlot>& wallpapers,
                                      const std::vector<models::DisplayMonitor>& displays,
                                      const std::vector<models::WallpaperLayoutModel>& disconnected,
                                      models::WallpaperArrangement arrangement);

// ---------------------------------------------------------------------------
// RestoreWallpaper(List<WallpaperLayoutModel>) — apply a saved layout.

// One entry handed to RestoreWallpaper. `queue_index` is the *identity* handle:
// the C# `wallpapersDisconnected.Contains(layout)` / `.Remove(layout)` are
// reference comparisons (WallpaperLayoutModel does not override Equals), so an
// entry that came from the queue must be distinguishable from a deserialized
// object that merely looks identical. Set it to the input-queue index for the
// former, nullopt for the latter — the two call sites differ exactly this way.
struct RestoreSource {
    models::WallpaperLayoutModel layout;
    std::optional<std::size_t> queue_index;
};

struct RestoreItem {
    std::string lively_info_path;
    std::optional<models::DisplayMonitor> screen;  // nullopt: screen is missing
    bool queued_to_disconnected = false;           // added to wallpapersDisconnected
    bool removed_from_disconnected = false;        // removed from wallpapersDisconnected
    bool library_unreadable = false;               // CreateFromDirectory threw
    bool restored = false;                         // SetWallpaperAsync was reached
};

struct RestoreApplyPlan {
    std::vector<RestoreItem> items;                            // in source order
    std::vector<models::WallpaperLayoutModel> disconnected;    // resulting queue
    // Input-queue indices dropped by `Remove` (a reference removal, so at most
    // one per source, and none at all for a source with no identity).
    std::vector<std::size_t> disconnected_removed;
};

// `disconnected` is the queue before the call; the plan carries it afterwards.
// `readable` reports whether CreateFromDirectory succeeds for a path, so the
// planner stays free of filesystem access.
//
// Deliberate deviation, documented at the definition: the C# continues after a
// failed CreateFromDirectory without a `continue`, so `libraryItem` stays null and
// the subsequent SetWallpaperAsync null-dereferences. The port implements the
// catch block's evident intent — drop the unreadable entry and skip it.
RestoreApplyPlan plan_restore(const std::vector<RestoreSource>& sources,
                              const std::vector<models::DisplayMonitor>& displays,
                              const std::vector<models::WallpaperLayoutModel>& disconnected,
                              const std::function<bool(const std::string&)>& readable);

// ---------------------------------------------------------------------------
// SaveWallpaperLayout() / the public RestoreWallpaper().

// C# SaveWallpaperLayout: every running wallpaper in order, then the disconnected
// queue *only* for `per`. Note the upstream dedupe is commented out, so the list
// can contain the same display twice — reproduced as-is.
std::vector<models::WallpaperLayoutModel> compose_layout(
    const std::vector<WallpaperSlot>& wallpapers,
    const std::vector<models::WallpaperLayoutModel>& disconnected,
    models::WallpaperArrangement arrangement);

// C# RestoreWallpaper(): span/duplicate re-create only layout[0] on the primary;
// `per` replays every entry.
struct StartupRestorePlan {
    bool replayed_each_entry = false;                 // per
    std::optional<std::string> single_path;           // span/duplicate
    models::DisplayMonitor single_screen;             // the primary
};

StartupRestorePlan plan_startup_restore(const std::vector<models::WallpaperLayoutModel>& layout,
                                        models::WallpaperArrangement arrangement,
                                        const models::DisplayMonitor& primary);

// ---------------------------------------------------------------------------
// Deterministic text rendering of a plan, for the oracle fixture.

std::string describe_refresh(const RefreshPlan& plan);
std::string describe_rect(const RectPlan& plan);
std::string describe_restore_disconnected(const RestorePlan& plan);
std::string describe_restore_apply(const RestoreApplyPlan& plan);
std::string describe_layout(const std::vector<models::WallpaperLayoutModel>& layout);
std::string describe_startup_restore(const StartupRestorePlan& plan);

} // namespace lively::core
