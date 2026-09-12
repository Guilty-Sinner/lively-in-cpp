#include <lively/core/desktop.h>

#include <lively/common/log_util.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>

namespace lively::core {

models::Rectangle screen_to_parent_rect(const models::Rectangle& display_bounds,
                                        const models::Rectangle& worker_w_rect) {
    models::Rectangle rect = display_bounds;
    rect.x -= worker_w_rect.x;
    rect.y -= worker_w_rect.y;
    return rect;
}

models::Rectangle span_rect(const models::Rectangle& worker_w_rect) {
    // The C# passes 0,0 for the position and `prct.Right - prct.Left` /
    // `prct.Bottom - prct.Top` for the size — the WorkerW's extent, not the
    // virtual screen's, because the WorkerW is already the whole desktop.
    models::Rectangle rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = worker_w_rect.width;
    rect.height = worker_w_rect.height;
    return rect;
}

#ifdef _WIN32

namespace {

constexpr UINT kSpawnWorkerWMessage = 0x052C;
constexpr int kSpawnWorkerWWParam = 0xD;
constexpr int kSpawnWorkerWLParam = 0x1;

constexpr UINT kSpiSetDeskWallpaper = 0x0014;
constexpr UINT kSpifUpdateIniFile = 0x0001;

constexpr UINT kSwpNoActivate = 0x0010;
constexpr UINT kSwpNoZOrder = 0x0004;
constexpr UINT kSwpNoMove = 0x0002;
constexpr UINT kSwpNoSize = 0x0001;

bool windows_7_cached = false;
bool windows_7_computed = false;

} // namespace

bool is_windows_7() {
    if (!windows_7_computed) {
        const DWORD version = GetVersion();
        const DWORD major = LOBYTE(LOWORD(version));
        const DWORD minor = HIBYTE(LOWORD(version));
        // Environment.OSVersion.Version on a manifest-unaware process reports the
        // real 6.1 for Windows 7 (and a capped version for later systems), which is
        // the same behaviour the C# relies on.
        windows_7_cached = (major == 6 && minor == 1);
        windows_7_computed = true;
    }
    return windows_7_cached;
}

int get_taskbar_explorer_pid() {
    const HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    if (tray != nullptr)
        GetWindowThreadProcessId(tray, &pid);
    return static_cast<int>(pid);
}

bool DesktopLayer::try_attach_to_desktop(win32::Hwnd hwnd) const {
    if (is_windows_7) {
        // Windows 7 has no WorkerW to parent to; Progman is the desktop.
        if (!win32::try_set_parent(hwnd, progman))
            return false;
    } else if (is_raised_desktop_with_layered_shell_view) {
        // Add WS_CHILD before SetParent, and WS_EX_LAYERED before SetParent too:
        // Godot (and other engines that create their own swapchain) fails to apply
        // WS_EX_LAYERED once the window is already a child.
        win32::set_window_style(hwnd, win32::kWS_CHILD);
        win32::set_window_transparency(hwnd, 255);

        if (!win32::try_set_parent(hwnd, progman))
            return false;

        // Z-order directly under the icon host: the wallpaper must render under
        // the DefView's icons but above the raw WorkerW background.
        const UINT flags = kSwpNoMove | kSwpNoSize | kSwpNoActivate;
        SetWindowPos(hwnd, shell_dll_def_view, 0, 0, 0, 0, flags);
        ensure_worker_w_z_order();
    } else {
        if (!win32::try_set_parent(hwnd, worker_w))
            return false;
    }
    return true;
}

bool DesktopLayer::set_wallpaper_per_screen(win32::Hwnd hwnd,
                                            const models::DisplayMonitor& display) const {
    if (hwnd == nullptr)
        return false;

    const auto& bounds = display.bounds;

    // (1) Position the wallpaper fullscreen on the target display, while it is
    // still a top-level window — this is what makes MapWindowPoints below map the
    // display's screen rectangle.
    if (SetWindowPos(hwnd, reinterpret_cast<HWND>(1), bounds.x, bounds.y,
                     bounds.width, bounds.height, kSwpNoActivate) == 0) {
        diagnostics.push_back(LIVELY_WIN32_ERROR("Failed to set perscreen wallpaper(1)"));
    }

    // (2) Capture the same rectangle in the parent's coordinate space.
    const models::Rectangle worker_rect = win32::get_window_rect(worker_w);
    const models::Rectangle mapped = screen_to_parent_rect(bounds, worker_rect);

    // (3) Adopt.
    const bool success = try_attach_to_desktop(hwnd);

    // (4) Re-position relative to the new parent. SWP_NOZORDER keeps the adoption
    // z-order from step 3.
    if (SetWindowPos(hwnd, reinterpret_cast<HWND>(1), mapped.x, mapped.y,
                     bounds.width, bounds.height, kSwpNoActivate | kSwpNoZOrder) == 0) {
        diagnostics.push_back(LIVELY_WIN32_ERROR("Failed to set perscreen wallpaper(2)"));
    }

    refresh_desktop();
    return success;
}

bool DesktopLayer::set_wallpaper_span_screen(win32::Hwnd hwnd) const {
    if (hwnd == nullptr)
        return false;

    const models::Rectangle worker_rect = win32::get_window_rect(worker_w);
    const bool success = try_attach_to_desktop(hwnd);

    const models::Rectangle rect = span_rect(worker_rect);
    if (SetWindowPos(hwnd, reinterpret_cast<HWND>(1), rect.x, rect.y,
                     rect.width, rect.height, kSwpNoActivate | kSwpNoZOrder) == 0) {
        diagnostics.push_back(LIVELY_WIN32_ERROR("Failed to set span wallpaper"));
    }

    refresh_desktop();
    return success;
}

void DesktopLayer::ensure_worker_w_z_order() const {
    if (!is_raised_desktop_with_layered_shell_view)
        return;

    if (win32::get_last_child_window(progman) != worker_w) {
        // Unexpected z-order: push the WorkerW to the bottom so it cannot cover
        // the icons.
        const UINT flags = kSwpNoMove | kSwpNoSize | kSwpNoActivate;
        SetWindowPos(worker_w, HWND_BOTTOM, 0, 0, 0, 0, flags);
    }
}

void DesktopLayer::refresh_desktop() const {
    // Refreshing destroys the current WorkerW on a raised desktop, so the C#
    // returns early there — the wallpaper would vanish on the next refresh.
    if (is_raised_desktop_with_layered_shell_view)
        return;
    SystemParametersInfoW(kSpiSetDeskWallpaper, 0, nullptr, kSpifUpdateIniFile);
}

bool DesktopLayer::is_desktop() const {
    const HWND foreground = GetForegroundWindow();
    return foreground == original_worker_w || foreground == progman;
}

#else   // !_WIN32

bool is_windows_7() { return false; }
int get_taskbar_explorer_pid() { return 0; }

bool DesktopLayer::try_attach_to_desktop(win32::Hwnd) const { return false; }
bool DesktopLayer::set_wallpaper_per_screen(win32::Hwnd, const models::DisplayMonitor&) const { return false; }
bool DesktopLayer::set_wallpaper_span_screen(win32::Hwnd) const { return false; }
void DesktopLayer::ensure_worker_w_z_order() const {}
void DesktopLayer::refresh_desktop() const {}
bool DesktopLayer::is_desktop() const { return false; }

#endif

DesktopLayer setup_desktop_layer() {
    DesktopLayer layer;
    layer.is_windows_7 = is_windows_7();

#ifdef _WIN32
    layer.progman = win32::get_progman();

    // Microsoft: when the desktop is split out from the list view window (the
    // "raised desktop"), Progman is created with WS_EX_NOREDIRECTIONBITMAP and the
    // DefView child becomes WS_EX_LAYERED. Detecting this changes which window the
    // wallpaper is parented to.
    layer.is_raised_desktop_with_layered_shell_view =
        win32::has_extended_style(layer.progman, win32::kWS_EX_NOREDIRECTIONBITMAP);

    // Send 0x052C to Progman: spawn a WorkerW behind the desktop icons. If it is
    // already there, nothing happens. SendMessageTimeout rather than SendMessage
    // because explorer.exe may be busy — a hung shell must not hang the app.
    if (layer.progman != nullptr) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(layer.progman, kSpawnWorkerWMessage,
                            static_cast<WPARAM>(kSpawnWorkerWWParam),
                            static_cast<LPARAM>(kSpawnWorkerWLParam),
                            SMTO_NORMAL, 1000, &result);
    }

    // Spy++ shape before 0x052C:
    //   0x00010190 "" WorkerW
    //     0x000100EE "" SHELLDLL_DefView
    //       0x000100F0 "FolderView" SysListView32
    //   0x00100B8A "" WorkerW        <-- the one we want
    //   0x000100EC "Program Manager" Progman
    // Enumerate top-level windows until one has SHELLDLL_DefView as a child, then
    // take the *next* WorkerW sibling.
    struct Search {
        DesktopLayer* layer;
    } search{&layer};

    EnumWindows([](HWND top, LPARAM lparam) -> BOOL {
        auto* state = reinterpret_cast<Search*>(lparam);
        HWND def_view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
        if (def_view != nullptr) {
            state->layer->worker_w = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
            state->layer->shell_dll_def_view = def_view;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));

    if (layer.is_raised_desktop_with_layered_shell_view) {
        // Spy++ shape after the split: the WorkerW is a child of Progman.
        layer.worker_w = FindWindowExW(layer.progman, nullptr, L"WorkerW", nullptr);
    }

    if (layer.is_windows_7) {
        // This should fix the wallpaper disappearing issue.
        if (layer.worker_w != layer.progman)
            ShowWindow(layer.worker_w, SW_HIDE);
        // WorkerW is assumed as progman here.
        layer.worker_w = layer.progman;
    }

    // For checking if the desktop is foreground.
    layer.original_worker_w = win32::get_desktop_worker_w();
#endif

    return layer;
}

} // namespace lively::core
