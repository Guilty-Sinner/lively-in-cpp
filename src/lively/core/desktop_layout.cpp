#include <lively/core/desktop_layout.h>

#include <algorithm>
#include <string>
#include <utility>

namespace lively::core {

namespace {

using models::DisplayMonitor;
using models::Rectangle;
using models::WallpaperArrangement;
using models::WallpaperLayoutModel;

// C# List<T>.Find / FirstOrDefault — the first element satisfying pred, in order.
template <typename T, typename Pred>
const T* find_first(const std::vector<T>& items, Pred pred) {
    for (const auto& item : items) {
        if (pred(item)) return &item;
    }
    return nullptr;
}

// C# List<T>.FindIndex — -1 when nothing matches.
template <typename T, typename Pred>
std::ptrdiff_t find_index(const std::vector<T>& items, Pred pred) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (pred(items[i])) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

std::string rect_text(const Rectangle& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "," +
           std::to_string(r.height);
}

} // namespace

bool is_orphan(const WallpaperSlot& wallpaper, const std::vector<DisplayMonitor>& displays) {
    return find_first(displays, [&](const DisplayMonitor& screen) {
               return wallpaper.on(screen);
           }) == nullptr;
}

bool is_multi_screen(const std::vector<DisplayMonitor>& displays) {
    // C# DisplayManager.IsMultiScreen() is exactly this — nothing about bounds.
    return displays.size() > 1;
}

namespace {

// C# DisplayManager.PrimaryDisplayMonitor =
// DisplayMonitors.FirstOrDefault(x => x.IsPrimary)
// (which is null when nothing is flagged primary — the port falls back to the
// first display instead of dereferencing null, and says so here).
DisplayMonitor primary_of(const std::vector<DisplayMonitor>& displays) {
    const DisplayMonitor* flagged =
        find_first(displays, [](const DisplayMonitor& d) { return d.is_primary; });
    if (flagged != nullptr) return *flagged;
    return displays.empty() ? DisplayMonitor{} : displays[0];
}

} // namespace

Rectangle virtual_screen_bounds(const std::vector<DisplayMonitor>& displays) {
    if (displays.empty()) return Rectangle{};
    int min_x = displays[0].bounds.x;
    int min_y = displays[0].bounds.y;
    int max_x = displays[0].bounds.x + displays[0].bounds.width;
    int max_y = displays[0].bounds.y + displays[0].bounds.height;
    for (const auto& d : displays) {
        min_x = std::min(min_x, d.bounds.x);
        min_y = std::min(min_y, d.bounds.y);
        max_x = std::max(max_x, d.bounds.x + d.bounds.width);
        max_y = std::max(max_y, d.bounds.y + d.bounds.height);
    }
    return Rectangle{min_x, min_y, max_x - min_x, max_y - min_y};
}

RefreshPlan plan_refresh(const std::vector<WallpaperSlot>& wallpapers,
                         const std::vector<DisplayMonitor>& displays,
                         const std::vector<WallpaperLayoutModel>& disconnected,
                         WallpaperArrangement arrangement, const DisplayMonitor& selected_display,
                         const DisplayMonitor& primary) {
    RefreshPlan plan;
    plan.refresh_desktop = true;

    // var orphanWallpapers = wallpapers.FindAll(w => allScreens.Find(s => w.Screen.Equals(s)) == null);
    for (const auto& wallpaper : wallpapers) {
        if (is_orphan(wallpaper, displays)) plan.orphans.push_back(wallpaper);
    }

    // Settings.SelectedDisplay = allScreens.Find(x => SelectedDisplay.Equals(x)) ?? Primary;
    const DisplayMonitor* match = find_first(displays, [&](const DisplayMonitor& screen) {
        return models::same_display(selected_display, screen);
    });
    plan.selected_display = match ? *match : primary;

    switch (arrangement) {
        case WallpaperArrangement::per:
            if (!plan.orphans.empty()) {
                // var newOrphans = orphanWallpapers.FindAll(old =>
                //     wallpapersDisconnected.Find(new2 => new2.Display.Equals(old.Screen)) == null);
                for (const auto& orphan : plan.orphans) {
                    const bool already_queued = find_first(disconnected,
                                                           [&](const WallpaperLayoutModel& queued) {
                                                               return queued.display &&
                                                                      models::same_display(
                                                                          *queued.display,
                                                                          orphan.screen);
                                                           }) != nullptr;
                    if (already_queued) continue;
                    // A FRESH model wrapping the orphan's own screen object.
                    plan.disconnected_added.emplace_back(
                        std::make_shared<DisplayMonitor>(orphan.screen), orphan.lively_info_path);
                }
                // The orphans are then disposed and removed from `wallpapers`; there
                // is no state to compute for that beyond `plan.orphans` above.
            }
            break;

        case WallpaperArrangement::duplicate:
            // Disposed and removed, but NOT queued: duplicate has no per-screen
            // memory.
            break;

        case WallpaperArrangement::span:
            // Only the wallpaper rect is updated.
            break;
    }

    return plan;
}

RectPlan plan_update_rect(const std::vector<WallpaperSlot>& wallpapers,
                          const std::vector<DisplayMonitor>& displays,
                          WallpaperArrangement arrangement) {
    RectPlan plan;
    plan.refresh_desktop = true;

    if (is_multi_screen(displays) && arrangement == WallpaperArrangement::span) {
        plan.span = true;
        if (wallpapers.empty()) return plan;  // C# `if (wallpapers.Count != 0)`

        RectUpdate update;
        update.index = 0;
        // Wallpapers[0].Screen = displayManager.PrimaryDisplayMonitor — the metadata
        // is rebound to the primary, then the window covers the virtual screen.
        update.screen = primary_of(displays);
        const Rectangle area = virtual_screen_bounds(displays);
        update.rect = Rectangle{0, 0, area.width, area.height};
        plan.span_update = update;
        return plan;
    }

    const Rectangle area = virtual_screen_bounds(displays);
    for (const auto& screen : displays) {
        const std::ptrdiff_t index = find_index(wallpapers, [&](const WallpaperSlot& wallpaper) {
            return wallpaper.on(screen);
        });
        if (index < 0) continue;
        RectUpdate update;
        update.index = static_cast<std::size_t>(index);
        update.screen = screen;
        // SetWindowPos(x, y, w, h) with the display's bounds relative to the
        // virtual-screen origin.
        update.rect = Rectangle{screen.bounds.x - area.x, screen.bounds.y - area.y,
                                screen.bounds.width, screen.bounds.height};
        plan.screen_updates.push_back(update);
    }
    return plan;
}

RestorePlan plan_restore_disconnected(const std::vector<WallpaperSlot>& wallpapers,
                                      const std::vector<DisplayMonitor>& displays,
                                      const std::vector<WallpaperLayoutModel>& disconnected,
                                      WallpaperArrangement arrangement) {
    RestorePlan plan;

    switch (arrangement) {
        case WallpaperArrangement::per:
            // wallpapersDisconnected.FindAll(w => displays.Find(s => w.Display.Equals(s)) != null)
            for (const auto& queued : disconnected) {
                if (!queued.display) continue;
                const bool connected = find_first(displays, [&](const DisplayMonitor& screen) {
                                           return models::same_display(*queued.display, screen);
                                       }) != nullptr;
                if (connected) plan.to_restore.push_back(queued);
            }
            break;

        case WallpaperArrangement::span:
            // UpdateWallpaperRect() handles it.
            break;

        case WallpaperArrangement::duplicate:
            // if (displays.Count > wallpapers.Count && wallpapers.Count != 0)
            if (displays.size() > wallpapers.size() && !wallpapers.empty()) {
                // First display with no wallpaper attached.
                const DisplayMonitor* new_screen = find_first(
                    displays, [&](const DisplayMonitor& screen) {
                        return find_first(wallpapers, [&](const WallpaperSlot& wallpaper) {
                                   return wallpaper.on(screen);
                               }) == nullptr;
                    });
                if (new_screen != nullptr) {
                    plan.duplicate_screen = *new_screen;
                    // Only one call is required for multiple screens: Wallpapers[0].
                    plan.duplicate_source_index = 0;
                }
            }
            break;
    }
    return plan;
}

RestoreApplyPlan plan_restore(const std::vector<RestoreSource>& sources,
                              const std::vector<DisplayMonitor>& displays,
                              const std::vector<WallpaperLayoutModel>& disconnected,
                              const std::function<bool(const std::string&)>& readable) {
    using QueueSlot = std::pair<std::optional<std::size_t>, WallpaperLayoutModel>;

    RestoreApplyPlan plan;
    std::vector<QueueSlot> queue;
    queue.reserve(disconnected.size());
    for (std::size_t i = 0; i < disconnected.size(); ++i) {
        queue.emplace_back(std::optional<std::size_t>(i), disconnected[i]);
    }
    // C# `Contains`/`Remove` on WallpaperLayoutModel are reference comparisons, so
    // a source with no identity handle can never match a queued entry.
    const auto identity_of = [](const QueueSlot& slot) { return slot.first; };
    const auto holds = [&](const std::optional<std::size_t>& id) {
        if (!id) return false;
        for (const auto& slot : queue) {
            if (identity_of(slot) == id) return true;
        }
        return false;
    };
    const auto drop = [&](const std::optional<std::size_t>& id) {
        if (!id) return false;
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (identity_of(*it) == id) {
                plan.disconnected_removed.push_back(*it->first);
                queue.erase(it);
                return true;
            }
        }
        return false;
    };

    for (const auto& source : sources) {
        RestoreItem item;
        item.lively_info_path = source.layout.lively_info_path.value_or(std::string());

        item.library_unreadable = !readable(item.lively_info_path);
        if (item.library_unreadable) {
            // C# catch block: wallpapersDisconnected.Remove(layout). Then the C#
            // falls through with a null libraryItem; the port skips instead (see
            // the header, and the note below).
            item.removed_from_disconnected = drop(source.queue_index);
            plan.items.push_back(item);
            continue;
        }

        const DisplayMonitor* screen = nullptr;
        if (source.layout.display) {
            // displays.FirstOrDefault(x => x.Equals(layout.Display))
            screen = find_first(displays, [&](const DisplayMonitor& candidate) {
                return models::same_display(candidate, *source.layout.display);
            });
        }

        if (screen == nullptr) {
            item.screen = std::nullopt;
            if (!holds(source.queue_index)) {
                // `new WallpaperLayoutModel((DisplayMonitor)layout.Display, layout.LivelyInfoPath)`
                // — a fresh object, so it gets a fresh identity.
                queue.emplace_back(std::optional<std::size_t>(),
                                   WallpaperLayoutModel(source.layout.display,
                                                        source.layout.lively_info_path));
                item.queued_to_disconnected = true;
            }
        } else {
            item.screen = *screen;
            item.restored = true;
            item.removed_from_disconnected = drop(source.queue_index);
        }
        plan.items.push_back(item);
    }

    for (const auto& slot : queue) plan.disconnected.push_back(slot.second);
    return plan;
}

std::vector<WallpaperLayoutModel> compose_layout(
    const std::vector<WallpaperSlot>& wallpapers,
    const std::vector<WallpaperLayoutModel>& disconnected, WallpaperArrangement arrangement) {
    std::vector<WallpaperLayoutModel> layout;
    layout.reserve(wallpapers.size() + disconnected.size());
    for (const auto& wallpaper : wallpapers) {
        layout.emplace_back(std::make_shared<DisplayMonitor>(wallpaper.screen),
                            wallpaper.lively_info_path);
    }
    // `if (arrangement == per) layout.AddRange(wallpapersDisconnected);` — the
    // upstream dedupe that used to follow this is commented out, so the list can
    // legitimately name the same display twice.
    if (arrangement == WallpaperArrangement::per) {
        for (const auto& queued : disconnected) layout.push_back(queued);
    }
    return layout;
}

StartupRestorePlan plan_startup_restore(const std::vector<WallpaperLayoutModel>& layout,
                                        WallpaperArrangement arrangement,
                                        const DisplayMonitor& primary) {
    StartupRestorePlan plan;
    if (arrangement == WallpaperArrangement::per) {
        plan.replayed_each_entry = true;
        return plan;
    }
    // span / duplicate: only the first entry, on the primary display.
    if (!layout.empty()) {
        plan.single_path = layout[0].lively_info_path.value_or(std::string());
        plan.single_screen = primary;
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Oracle renderings. One line per fact, tabs avoided, deterministic.

namespace {

std::string screen_id(const DisplayMonitor& display) {
    return display.device_id.empty() ? std::string("<none>") : display.device_id;
}

} // namespace

std::string describe_refresh(const RefreshPlan& plan) {
    std::string out;
    out += "selected=" + screen_id(plan.selected_display);
    out += ";orphans=" + std::to_string(plan.orphans.size());
    for (const auto& orphan : plan.orphans) {
        out += "[" + screen_id(orphan.screen) + "|" + orphan.lively_info_path + "]";
    }
    out += ";queued=" + std::to_string(plan.disconnected_added.size());
    for (const auto& queued : plan.disconnected_added) {
        out += "[" + (queued.display ? screen_id(*queued.display) : std::string("<none>")) + "|" +
               queued.lively_info_path.value_or(std::string()) + "]";
    }
    out += ";refreshDesktop=" + std::string(plan.refresh_desktop ? "True" : "False");
    return out;
}

std::string describe_rect(const RectPlan& plan) {
    std::string out;
    out += "span=" + std::string(plan.span ? "True" : "False");
    if (plan.span_update) {
        out += ";spanUpdate=" + std::to_string(plan.span_update->index) + "@" +
               screen_id(plan.span_update->screen) + ":" + rect_text(plan.span_update->rect);
    } else {
        out += ";spanUpdate=<none>";
    }
    out += ";updates=" + std::to_string(plan.screen_updates.size());
    for (const auto& update : plan.screen_updates) {
        out += "[" + std::to_string(update.index) + "@" + screen_id(update.screen) + ":" +
               rect_text(update.rect) + "]";
    }
    return out;
}

std::string describe_restore_disconnected(const RestorePlan& plan) {
    std::string out;
    out += "toRestore=" + std::to_string(plan.to_restore.size());
    for (const auto& queued : plan.to_restore) {
        out += "[" + (queued.display ? screen_id(*queued.display) : std::string("<none>")) + "|" +
               queued.lively_info_path.value_or(std::string()) + "]";
    }
    out += ";duplicateScreen=" +
           (plan.duplicate_screen ? screen_id(*plan.duplicate_screen) : std::string("<none>"));
    if (plan.duplicate_screen) {
        out += ";duplicateSourceIndex=" + std::to_string(plan.duplicate_source_index);
    }
    return out;
}

std::string describe_restore_apply(const RestoreApplyPlan& plan) {
    std::string out;
    for (const auto& item : plan.items) {
        out += "[" + item.lively_info_path;
        // The oracle reports <unreadable> before it resolves the screen, matching
        // WinDesktopCore's CreateFromDirectory-then-lookup order.
        out += "|screen=" + (item.library_unreadable
                                 ? std::string("<unreadable>")
                                 : (item.screen ? screen_id(*item.screen) : std::string("<missing>")));
        out += "|unreadable=" + std::string(item.library_unreadable ? "True" : "False");
        out += "|queued=" + std::string(item.queued_to_disconnected ? "True" : "False");
        out += "|restored=" + std::string(item.restored ? "True" : "False");
        out += "|removed=" + std::string(item.removed_from_disconnected ? "True" : "False");
        out += "]";
    }
    out += ";removedIndices=" + std::to_string(plan.disconnected_removed.size());
    for (const std::size_t index : plan.disconnected_removed) {
        out += "[" + std::to_string(index) + "]";
    }
    out += ";queue=" + describe_layout(plan.disconnected);
    return out;
}

std::string describe_layout(const std::vector<WallpaperLayoutModel>& layout) {
    std::string out;
    out += std::to_string(layout.size());
    for (const auto& entry : layout) {
        out += "[" + (entry.display ? screen_id(*entry.display) : std::string("<none>")) + "|" +
               entry.lively_info_path.value_or(std::string("<null>")) + "]";
    }
    return out;
}

std::string describe_startup_restore(const StartupRestorePlan& plan) {
    std::string out;
    out += "replayEach=" + std::string(plan.replayed_each_entry ? "True" : "False");
    out += ";single=" + (plan.single_path ? *plan.single_path : std::string("<none>"));
    if (plan.single_path) {
        out += ";screen=" + screen_id(plan.single_screen);
    }
    return out;
}

} // namespace lively::core
