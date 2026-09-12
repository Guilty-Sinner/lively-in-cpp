#include <lively/core/win32/desktop_util.h>

#ifdef _WIN32
#include <windows.h>

// SHELLSTATE + SHGetSetSettings + SSF_HIDEICONS. Taken from the real header
// rather than re-declared: SSF_HIDEICONS is 0x00004000, not the low bit, and a
// hand-rolled mask would silently read the wrong flag.
#include <shlobj.h>
#endif

namespace lively::core::win32 {

#ifdef _WIN32

Hwnd get_progman() {
    return FindWindowW(L"Progman", nullptr);
}

Hwnd get_desktop_worker_w() {
    const Hwnd progman = get_progman();
    Hwnd worker_w_orig = nullptr;

    Hwnd folder_view = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (folder_view == nullptr) {
        // If the desktop isn't under Progman, cycle through the WorkerW handles
        // and find the correct one.
        do {
            worker_w_orig = FindWindowExW(GetDesktopWindow(), worker_w_orig, L"WorkerW", nullptr);
            folder_view = FindWindowExW(worker_w_orig, nullptr, L"SHELLDLL_DefView", nullptr);
        } while (folder_view == nullptr && worker_w_orig != nullptr);
    }
    // Newer versions of Windows 11 (with layered ShellView): Progman is the
    // worker, because no separate WorkerW hosts the icons.
    return worker_w_orig != nullptr ? worker_w_orig : progman;
}

Hwnd get_desktop_shell_dll_def_view() {
    Hwnd shell_view = nullptr;
    Hwnd worker_w = nullptr;

    const Hwnd progman = FindWindowW(L"Progman", L"Program Manager");
    const Hwnd desktop_wnd = GetDesktopWindow();

    if (progman != nullptr) {
        shell_view = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (shell_view == nullptr) {
            // When this fails (picture rotation is turned on), look through the
            // WorkerW list to get the correct desktop list handle. There can be
            // several WorkerW windows, so iterate.
            do {
                worker_w = FindWindowExW(desktop_wnd, worker_w, L"WorkerW", nullptr);
                shell_view = FindWindowExW(worker_w, nullptr, L"SHELLDLL_DefView", nullptr);
            } while (shell_view == nullptr && worker_w != nullptr);
        }
    }
    return shell_view;
}

bool get_desktop_icon_visibility() {
    // DesktopUtil.GetDesktopIconVisibility: read SHELLSTATE and return
    // !fHideIcons. The struct is zero-initialised because SHGetSetSettings only
    // fills the fields the mask selects.
    SHELLSTATE state{};
    SHGetSetSettings(&state, SSF_HIDEICONS, FALSE);   // get
    return !state.fHideIcons;
}

void set_desktop_icon_visibility(bool visible) {
    // SHGetSetSettings(..., true) is not working in Windows 10 — send the shell
    // command instead, and only when the state actually differs.
    constexpr WPARAM kCmdToggleDesktopIcons = 0x7402;
    constexpr UINT kWmCommand = 0x0111;
    if (get_desktop_icon_visibility() != visible) {
        const Hwnd def_view = get_desktop_shell_dll_def_view();
        if (def_view != nullptr)
            SendMessageW(def_view, kWmCommand, kCmdToggleDesktopIcons, 0);
    }
}

#else

Hwnd get_progman() { return nullptr; }
Hwnd get_desktop_worker_w() { return nullptr; }
Hwnd get_desktop_shell_dll_def_view() { return nullptr; }
bool get_desktop_icon_visibility() { return true; }
void set_desktop_icon_visibility(bool) {}

#endif

} // namespace lively::core::win32
