#pragma once
// Port of Lively.Common/Helpers/Shell/DesktopUtil.cs — finding the windows the
// desktop is made of.
//
// The whole file exists because "the desktop" is not one window and has not been
// for a while. On Windows 7/10 the wallpaper host is a WorkerW window that sits
// behind the icon list (SHELLDLL_DefView); after Windows 11 split the desktop out
// of the list view (the "raised desktop"), Progman itself is created with
// WS_EX_NOREDIRECTIONBITMAP and the shell DefView becomes a WS_EX_LAYERED child,
// so the window to parent onto is a WorkerW child *of Progman* instead of a
// sibling. Getting this wrong is the classic "wallpaper appears behind the
// icons / disappears when you press Win+D" failure, which is why the port keeps
// both discovery paths and the same documented order.
//
// Progman/DefView/WorkerW window *classes* are shell internals: there is no
// supported API for any of this, only the enduring window-class names. The
// comments in the C# quote Microsoft's own explanation of the raised-desktop
// change; that context is kept here because a future Windows could rename any of
// these and this is where it would be noticed.

#include <lively/core/win32/window_util.h>

#include <string>

namespace lively::core::win32 {

// DesktopUtil.GetProgman: FindWindow("Progman", null).
Hwnd get_progman();

// DesktopUtil.GetDesktopWorkerW: the WorkerW that hosts SHELLDLL_DefView, i.e.
// the window that draws the desktop icons. Falls back to Progman when the class
// search finds nothing (newer Windows 11 with a layered ShellView), because
// Progman is then the correct parent.
Hwnd get_desktop_worker_w();

// DesktopUtil.GetDesktopSHELLDLL_DefView: the icon list view, searching under
// Progman first and then across the WorkerW list (the case when picture rotation
// is enabled). Zero when not found.
Hwnd get_desktop_shell_dll_def_view();

// DesktopUtil.GetDesktopIconVisibility / SetDesktopIconVisibility.
// The setter is the documented workaround: SHGetSetSettings' SSF_HIDEICONS is
// broken on Windows 10, so the toggle is sent as WM_COMMAND 0x7402 to the DefView
// window, and only when the current state differs (toggling unconditionally
// would flip the icons every call).
bool get_desktop_icon_visibility();
void set_desktop_icon_visibility(bool visible);

} // namespace lively::core::win32
