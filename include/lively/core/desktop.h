#pragma once
// Port of the window-adoption half of Lively/Core/WinDesktopCore.cs.
//
// The decision half (which wallpaper belongs on which screen) is
// include/lively/core/desktop_layout.h. This is what happens *after* each
// decision: making an arbitrary window stop being a floating window and become
// the desktop background, at a chosen rectangle, on a chosen monitor.
//
// Three regimes, all of which must be handled because all three are live:
//
//   1. Windows 7 — WorkerW does not exist; Progman is the parent and the WorkerW
//      the enumeration found is explicitly hidden first.
//   2. Windows 10 — a WorkerW sibling of Progman hosts SHELLDLL_DefView; the
//      wallpaper window is parented to the *next* WorkerW, which is the one that
//      draws behind the icons.
//   3. Windows 11 "raised desktop" — Progman is created without a redirection
//      bitmap and the DefView becomes a layered child, so the wallpaper is
//      parented to Progman and z-ordered *under* the DefView, and must be given
//      WS_EX_LAYERED before SetParent (upstream note: some engines fail to apply
//      it afterwards).
//
// The failure mode this file exists to avoid is silent: parent to the wrong
// window and the wallpaper is simply not visible, or it disappears the moment the
// user presses Win+D or clicks the desktop. There is no error to observe.
//
// Rect math is split out as a pure function so the negative-coordinate case can be
// tested: a monitor left of the primary has a negative X, and the span rect is
// deliberately NOT rebased (it is the WorkerW's own rect, which starts at 0,0).

#include <lively/core/win32/desktop_util.h>
#include <lively/core/win32/window_util.h>
#include <lively/models/display_monitor.h>

#include <optional>
#include <string>
#include <vector>

namespace lively::core {

namespace win32 = core::win32;

// `TrySetWallpaperPerScreen`'s rect: the display's bounds expressed in the parent
// window's client space. C# reaches this by positioning the window at the screen
// bounds and then MapWindowPoints(handle, workerW, rect, 2); since the player
// window is borderless by the time it gets here, the mapping is exactly
// `display.bounds - worker_w_rect.origin`.
models::Rectangle screen_to_parent_rect(const models::Rectangle& display_bounds,
                                        const models::Rectangle& worker_w_rect);

// `TrySetWallpaperSpanScreen`'s rect: the WorkerW's own rectangle, at (0,0) and
// the full virtual-screen size.
models::Rectangle span_rect(const models::Rectangle& worker_w_rect);

// The resolved desktop windows, i.e. the state `SetupDesktopLayer` produces.
struct DesktopLayer {
    win32::Hwnd progman = nullptr;
    win32::Hwnd worker_w = nullptr;
    win32::Hwnd shell_dll_def_view = nullptr;
    // The WorkerW that hosts the desktop icons *before* 0x052C is sent — captured
    // because it is the window that receives focus when the desktop is clicked,
    // which is how the core tells "desktop is foreground" from "a wallpaper is".
    win32::Hwnd original_worker_w = nullptr;
    bool is_raised_desktop_with_layered_shell_view = false;
    bool is_windows_7 = false;

    // The C# logs every failed SetWindowPos through LogUtil.GetWin32Error and
    // carries on (a failed reposition is not fatal — the adoption already
    // happened). The port has no logger yet, so the same strings are collected
    // here for the caller to surface; discarding them would make "the wallpaper
    // is on the wrong monitor" undiagnosable.
    mutable std::vector<std::string> diagnostics;

    // TryAttachToDesktop: reparent `hwnd` onto the desktop, choosing the regime.
    bool try_attach_to_desktop(win32::Hwnd hwnd) const;
    // The full per-screen sequence: position, map, attach, re-position relative
    // to the new parent. Returns false when the attach failed.
    bool set_wallpaper_per_screen(win32::Hwnd hwnd, const models::DisplayMonitor& display) const;
    // The span sequence: the window fills the whole WorkerW.
    bool set_wallpaper_span_screen(win32::Hwnd hwnd) const;
    // EnsureWorkerWZOrder: on a raised desktop, the WorkerW must be the last
    // child of Progman, otherwise it is pushed to HWND_BOTTOM.
    void ensure_worker_w_z_order() const;
    // RefreshDesktop: SPI_SETDESKWALLPAPER. No-op on a raised desktop, because
    // the refresh itself would destroy the WorkerW the wallpaper is parented to.
    void refresh_desktop() const;
    // IsDesktop: is the foreground window the desktop (the icon host or Progman)?
    bool is_desktop() const;
};

// WinDesktopCore.SetupDesktopLayer: find Progman, detect the raised-desktop
// regime, send the undocumented 0x052C to spawn a WorkerW behind the icons, then
// locate it.
DesktopLayer setup_desktop_layer();

// WinDesktopCore.IsDesktop's companion: GetWindowThreadProcessId of Shell_TrayWnd
// — used to notice explorer.exe restarting, which invalidates every adoption.
int get_taskbar_explorer_pid();

// IsWindows7 (Environment.OSVersion 6.1). The port reads the version through
// RtlGetVersion semantics indirectly: the C# compares the *reported* version, so
// an application-compatibility shim would change this — same exposure, and it is
// why the check is kept separate rather than folded into the other branches.
bool is_windows_7();

} // namespace lively::core
