#pragma once
// Port of Lively.Common/Helpers/WindowUtil.cs.
//
// Split deliberately into two groups:
//
//   * The geometry predicates (is_window_covering_target,
//     is_display_covered_by_window_grid) are pure functions over rectangles.
//     They decide whether a fullscreen window is covering a display, which is
//     what the playback-suspend logic and the screensaver idle checks use, and
//     they are testable without a desktop — so they are, in tests/test_win32.cpp.
//   * Everything else calls Win32 on live HWNDs. `GetWindowRect` on a handle
//     that closed between the enumeration and the call is a real, frequent case
//     here (the C# `IsDisplayCoveredByWindowGrid` explicitly skips the zero-rect
//     result), and the port keeps the same tolerance.
//
// `IsDisplayCoveredByWindowGrid` is the one with subtle behaviour worth naming:
// the C# walks a 50px grid and returns true as soon as the *uncovered* fraction
// drops to the threshold, and it short-circuits on the first maximized window
// (`IsZoomed`) or on any window that alone covers >= 95% of the screen. A port
// that computed exact union area instead would answer differently for a screen
// fully covered by overlapping windows whose union it mis-measures, and would
// not short-circuit at all.

#include <lively/models/display_monitor.h>

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lively::core::win32 {

#ifdef _WIN32
using Hwnd = HWND;
#else
using Hwnd = void*;
#endif

// WindowStyles bits used by the call sites below (Windows' own values).
inline constexpr std::uint32_t kWS_CHILD = 0x40000000u;
inline constexpr std::uint32_t kWS_EX_LAYERED = 0x00080000u;
inline constexpr std::uint32_t kWS_EX_TRANSPARENT = 0x00000020u;
inline constexpr std::uint32_t kWS_EX_TOOLWINDOW = 0x00000080u;
inline constexpr std::uint32_t kWS_EX_APPWINDOW = 0x00040000u;
inline constexpr std::uint32_t kWS_EX_NOACTIVATE = 0x08000000u;
inline constexpr std::uint32_t kWS_EX_NOREDIRECTIONBITMAP = 0x00200000u;

// ---------------------------------------------------------------------------
// Pure geometry

// WindowUtil.IsWindowCoveringTarget(Rectangle, Rectangle, threshold):
// intersection area / target area >= threshold. A zero-area target is false
// (the C# divides by it, which would be a division by zero for an int/long
// target; the port guards because a monitor can legitimately report 0x0 while
// it is being reconfigured).
bool is_window_covering_target(const models::Rectangle& window_rect,
                               const models::Rectangle& target_area,
                               double threshold = 0.95);

// WindowUtil.IsDisplayCoveredByWindowGrid. `window_rects` are the top-level
// windows captured from GetWindowRect; `maximized` mirrors the C#
// `topLevelWindows.Exists(IsZoomed)` check, which is a separate Win32
// interrogation in the C# and so is an input here.
bool is_display_covered_by_window_grid(const std::vector<models::Rectangle>& window_rects,
                                       bool any_window_maximized,
                                       const models::Rectangle& screen_bounds,
                                       int tile_size = 50,
                                       double threshold = 0.05);

// WindowUtil.IsDisplayCoveredByAnyWindow: any window alone covering the screen.
bool is_display_covered_by_any_window(const std::vector<models::Rectangle>& window_rects,
                                      const models::Rectangle& screen_bounds,
                                      double threshold = 0.95);

// ---------------------------------------------------------------------------
// Live HWND helpers (no-ops / false without a window, like the C# null checks)

// WindowUtil.TrySetParent: SetParent(child, parent) != NULL.
bool try_set_parent(Hwnd child, Hwnd parent);
// WindowUtil.GetLastChildWindow: the last handle EnumChildWindows yields, which
// is the z-order bottom child; used to detect an unexpected WorkerW position.
Hwnd get_last_child_window(Hwnd parent);
// WindowUtil.SetWindowTransparency: add WS_EX_LAYERED if absent, then
// SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA).
void set_window_transparency(Hwnd hwnd, std::uint8_t alpha = 255);
// WindowUtil.SetWindowStyle / SetWindowExStyle: OR a style in.
void set_window_style(Hwnd hwnd, std::int64_t style_to_add);
void set_window_ex_style(Hwnd hwnd, std::int64_t ex_style_to_add);
// WindowUtil.HasExtendedStyle.
bool has_extended_style(Hwnd hwnd, std::uint32_t style);
std::int64_t get_window_ex_style(Hwnd hwnd);

// WindowUtil.HasClass — ordinal case-insensitive, like the C# comparison.
bool has_class(Hwnd hwnd, const std::string& expected_class_name);
std::string get_class_name(Hwnd hwnd);

// WindowUtil.RemoveWindowFromTaskbar: hide, OR in WS_EX_NOACTIVATE and
// WS_EX_TOOLWINDOW, show. Called on every adopted player window so it cannot
// appear in Alt+Tab.
void remove_window_from_taskbar(Hwnd hwnd);

// WindowUtil.BorderlessWinStyle: strip caption/frame/sysmenu and the modal
// frame styles, then remove the menu. The mpv host calls this before adopting
// the child window, which is what stops a title bar from being drawn inside the
// desktop.
void borderless_win_style(Hwnd hwnd);

// WindowUtil.IsTopLevelWindow: GetAncestor(hwn, GetRoot) == hwnd.
bool is_top_level_window(Hwnd hwnd);
// WindowUtil.IsCloakedWindow: DwmGetWindowAttribute(DWMWA_CLOAKED).
bool is_cloaked_window(Hwnd hwnd);
// WindowUtil.IsVisibleTopLevelWindows — the full predicate, including the
// WS_EX_NOACTIVATE/WS_EX_APPWINDOW pairing and the non-empty-title rule.
bool is_visible_top_level_window(Hwnd hwnd);
// WindowUtil.GetVisibleTopLevelWindows.
std::vector<Hwnd> get_visible_top_level_windows();

// Rectangle conversion for GetWindowRect (rect.right/bottom are exclusive
// edges, so width = right - left, exactly as the C# does).
models::Rectangle to_rectangle(int left, int top, int right, int bottom);
models::Rectangle get_window_rect(Hwnd hwnd);
bool get_window_rect(Hwnd hwnd, models::Rectangle& out);

} // namespace lively::core::win32
