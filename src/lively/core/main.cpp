// lively_core — the desktop core of Lively.
//
// The C# app's `Lively` project is a WPF/WinUI host: it owns the tray icon, the
// gRPC servers, the settings service and the UI, and WinDesktopCore sits behind
// all of that. This binary is the part of that project that can exist without a
// UI toolkit: the desktop layer (WorkerW discovery and adoption), the wallpaper
// factory, and the player hosts.
//
// What it is for, concretely: it is the executable that can put something on the
// desktop on a real machine, and it is the only way to check the parts of the
// port that no unit test can reach — whether the WorkerW this Windows build
// produces is the one the code expects, whether the mpv child's window really can
// be adopted, whether a picture wallpaper really replaces the background. Every
// other tranche in this repository is either transcript-verified or
// logic-verified; this one is verified by looking at the screen.
//
// Usage:
//   lively_core workerw                     report the resolved desktop layer
//   lively_core set <path> [options]        show a wallpaper until interrupted
//   lively_core help
//
// `set` options:
//   --type=<name>        video | gif | videostream | picture | web  (default:
//                        inferred from the file extension, then the folder's
//                        LivelyInfo.json)
//   --screen=<deviceId>  target display (default: the primary)
//   --scaling=<name>     none | fill | uniform | uniformFill | auto
//   --arrangement=<name> per | span | duplicate
//   --no-hwdec           disable hardware decoding for the mpv host
//   --seconds=<n>        run for n seconds instead of until Ctrl+C
//   --no-adopt           launch the player but do not parent it onto the desktop
//                        (a diagnostic: the window stays a normal window, so the
//                        desktop adoption can be judged on its own)
#include <lively/common/constants.h>
#include <lively/common/factories/wallpaper_library_factory.h>
#include <lively/common/file_types.h>
#include <lively/core/desktop.h>
#include <lively/core/display_manager.h>
#include <lively/core/mpv_wallpaper.h>
#include <lively/core/picture_wallpaper.h>
#include <lively/core/win32/desktop_util.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace lively;
using namespace lively::core;

// The directory the app's own files live in (Constants.PlayerPartialPaths are
// resolved against it). The C# uses AppDomain.CurrentDomain.BaseDirectory; here
// it is the executable's own directory, which is the same thing for a deployed
// build and the build directory for a dev one — where plugins/mpv/ would be.
fs::path app_base_directory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0)
        return fs::path(std::wstring(buffer, length)).parent_path();
#endif
    return fs::current_path();
}

// The core builds its DisplayManager once at startup and refreshes it on
// WM_DISPLAYCHANGE; the CLI builds one per invocation.
DisplayManager make_display_manager() {
    DisplayManager manager;
    manager.refresh();
    return manager;
}

std::string lower(std::string value) {
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

struct SetOptions {
    std::string path;
    std::optional<std::string> type;
    std::optional<std::string> screen_device_id;
    models::WallpaperScaler scaling = models::WallpaperScaler::uniformFill;
    models::WallpaperArrangement arrangement = models::WallpaperArrangement::per;
    bool hw_decode = true;
    std::optional<int> seconds;
    bool adopt = true;
};

// Type inference mirrors the library factory's own rules closely enough for a
// diagnostic binary: the LivelyInfo.json in the folder wins, then the file
// extension.
std::optional<models::WallpaperType> parse_type(const std::string& name) {
    const std::string value = lower(name);
    if (value == "video") return models::WallpaperType::video;
    if (value == "gif") return models::WallpaperType::gif;
    if (value == "videostream") return models::WallpaperType::videostream;
    if (value == "picture") return models::WallpaperType::picture;
    if (value == "web") return models::WallpaperType::web;
    if (value == "webaudio") return models::WallpaperType::webaudio;
    if (value == "app") return models::WallpaperType::app;
    return std::nullopt;
}

std::optional<models::WallpaperScaler> parse_scaler(const std::string& name) {
    const std::string value = lower(name);
    if (value == "none") return models::WallpaperScaler::none;
    if (value == "fill") return models::WallpaperScaler::fill;
    if (value == "uniform") return models::WallpaperScaler::uniform;
    if (value == "uniformfill") return models::WallpaperScaler::uniformFill;
    if (value == "auto") return models::WallpaperScaler::autofit;
    return std::nullopt;
}

std::optional<models::WallpaperArrangement> parse_arrangement(const std::string& name) {
    const std::string value = lower(name);
    if (value == "per") return models::WallpaperArrangement::per;
    if (value == "span") return models::WallpaperArrangement::span;
    if (value == "duplicate") return models::WallpaperArrangement::duplicate;
    return std::nullopt;
}

void print_usage() {
    std::puts(
        "lively_core — Lively desktop core (C++ port)\n"
        "\n"
        "  lively_core workerw                 report the resolved desktop layer\n"
        "  lively_core adopt-test [--span] [--seconds=n]\n"
        "                                      adopt a plain window onto the desktop\n"
        "  lively_core set <path> [options]    show a wallpaper until interrupted\n"
        "  lively_core help\n"
        "\n"
        "set options:\n"
        "  --type=video|gif|videostream|picture|web\n"
        "  --screen=<deviceId>   target display (default: primary)\n"
        "  --scaling=none|fill|uniform|uniformFill|auto\n"
        "  --arrangement=per|span|duplicate\n"
        "  --no-hwdec\n"
        "  --seconds=<n>\n"
        "  --no-adopt            leave the player window floating (diagnostic)\n");
}

// lively_core workerw — the diagnostic that answers the only question unit tests
// cannot: is the window this Windows build calls the desktop the one the port
// expects? Prints the same values WinDesktopCore logs during SetupDesktopLayer.
int run_workerw() {
    const DesktopLayer layer = setup_desktop_layer();
    const DisplayManager displays = make_display_manager();

#ifdef _WIN32
    std::printf("progman                       = 0x%p\n", static_cast<void*>(layer.progman));
    std::printf("workerW                       = 0x%p\n", static_cast<void*>(layer.worker_w));
    std::printf("shellDLL_DefView              = 0x%p\n", static_cast<void*>(layer.shell_dll_def_view));
    std::printf("original WorkerW              = 0x%p\n", static_cast<void*>(layer.original_worker_w));
#else
    std::printf("progman                       = (non-Windows build)\n");
#endif
    std::printf("raised desktop (layered shell)= %s\n",
                layer.is_raised_desktop_with_layered_shell_view ? "yes" : "no");
    std::printf("windows 7 regime              = %s\n", layer.is_windows_7 ? "yes" : "no");
    std::printf("desktop icons visible         = %s\n",
                win32::get_desktop_icon_visibility() ? "yes" : "no");
    std::printf("taskbar explorer pid          = %d\n", get_taskbar_explorer_pid());

    std::printf("\nmonitors (%zu, multi=%s):\n", displays.displays().size(),
                displays.is_multi_screen() ? "yes" : "no");
    for (const auto& display : displays.displays()) {
        std::printf("  [%d] %-16s %s bounds=%-24s work=%s\n", display.index,
                    display.device_name.c_str(),
                    display.is_primary ? "primary" : "       ",
                    models::rectangle_to_string(display.bounds).c_str(),
                    models::rectangle_to_string(display.working_area).c_str());
        std::printf("      id=%s\n", display.device_id.c_str());
        std::printf("      display=%s\n", display.display_name.c_str());
    }
    std::printf("virtual screen                = %s\n",
                models::rectangle_to_string(displays.virtual_screen_bounds()).c_str());

    if (!layer.diagnostics.empty()) {
        std::printf("\ndiagnostics:\n");
        for (const auto& line : layer.diagnostics)
            std::printf("  %s\n", line.c_str());
    }

    if (layer.progman == nullptr) {
        std::fprintf(stderr, "\nerror: Progman not found — is explorer.exe running?\n");
        return 1;
    }
    // Both regimes need a WorkerW; without one the wallpaper would be parented to
    // nothing and simply not appear.
    if (layer.worker_w == nullptr) {
        std::fprintf(stderr, "\nerror: WorkerW not found; the desktop cannot be adopted.\n");
        return 1;
    }
    return 0;
}

// lively_core adopt-test — the diagnostic for the one part of this port that no
// test can reach and that fails silently when it is wrong.
//
// It creates a plain Win32 window filled with a colour, adopts it onto the
// desktop exactly the way a wallpaper player window is adopted
// (set_wallpaper_per_screen / set_wallpaper_span_screen), holds it for a few
// seconds, and destroys it. If the adoption is right, the colour replaces the
// desktop background *behind the icons*; if the parent or the rect rebasing is
// wrong, the window either floats on top, sits behind nothing visible, or
// vanishes the moment the user presses Win+D.
//
// It deliberately does NOT touch IDesktopWallpaper or SystemParametersInfo, so it
// leaves no trace on the user's own wallpaper — the desktop background is restored
// by Windows as soon as the window is destroyed.

#ifdef _WIN32

struct AdoptTestState {
    bool span = false;
    COLORREF colour = RGB(32, 96, 192);
};

LRESULT CALLBACK adopt_test_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            const auto* state = reinterpret_cast<const AdoptTestState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            HBRUSH brush = CreateSolidBrush(state ? state->colour : RGB(32, 96, 192));
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            // Avoids the flash between the class brush and the WM_PAINT fill.
            return 1;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

#endif

int run_adopt_test(bool span, int seconds, bool force_classic_regime) {
#ifndef _WIN32
    std::fprintf(stderr, "adopt-test requires Windows.\n");
    (void)span;
    (void)seconds;
    return 1;
#else
    static AdoptTestState state;
    state.span = span;

    const wchar_t* kClassName = L"LivelyAdoptTestWindow";
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = adopt_test_proc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;   // WM_PAINT fills it
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::fprintf(stderr, "error: RegisterClassEx failed\n");
        return 1;
    }

    // No caption and no thick frame, the same starting point as a player window
    // after WindowUtil.BorderlessWinStyle.
    HWND hwnd = CreateWindowExW(0, kClassName, L"LivelyAdoptTest",
                                WS_POPUP | WS_VISIBLE, 0, 0, 640, 480,
                                nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        std::fprintf(stderr, "error: CreateWindowEx failed\n");
        return 1;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    DesktopLayer layer = setup_desktop_layer();
    if (force_classic_regime) {
        // Compare the two adoption regimes on the machine that is actually running,
        // which is the only way to tell a genuine misread of the desktop layout
        // from a Windows build behaving differently than the C# assumes.
        layer.is_raised_desktop_with_layered_shell_view = false;
        std::printf("classic regime forced (parent will be workerW)\n");
    }
    const DisplayManager manager = make_display_manager();
    if (manager.displays().empty()) {
        std::fprintf(stderr, "error: no displays\n");
        DestroyWindow(hwnd);
        return 1;
    }
    // The target the core would pick: the primary display.
    const models::DisplayMonitor target = manager.primary_display().value_or(manager.displays().front());

    std::printf("before   : parent=0x%p owner=0x%p\n",
                static_cast<void*>(GetParent(hwnd)), static_cast<void*>(GetWindow(hwnd, GW_OWNER)));
    std::printf("adopting %s on %s (%s)\n", span ? "span" : "per-screen",
                target.device_name.c_str(),
                models::rectangle_to_string(target.bounds).c_str());

    // SetParent's return value is the *previous* parent, which is NULL for a
    // top-level window — indistinguishable from failure. WindowUtil.TrySetParent
    // tests it against NULL anyway (as the C# does), so the C# contract is what
    // `attached` reports; `parent` is the independent check, and it is the one that
    // decides whether the wallpaper is actually on the desktop.
    SetLastError(ERROR_SUCCESS);
    const bool attached = span ? layer.set_wallpaper_span_screen(hwnd)
                               : layer.set_wallpaper_per_screen(hwnd, target);
    const DWORD attach_error = GetLastError();

    // Read the parent with GetAncestor(hwnd, GA_PARENT), NOT GetParent.
    //
    // This is the trap the first version of this diagnostic fell into: an adopted
    // window carries WS_POPUP and WS_CHILD at the same time (WS_CHILD is OR-ed in
    // by TryAttachToDesktop; nothing clears WS_POPUP because the player created the
    // window that way), and GetParent answers the *popup* branch for such a window —
    // it returns the owner, which is NULL. The reparenting had in fact succeeded:
    // GA_PARENT is the WorkerW (classic) or Progman (raised desktop) exactly as
    // intended. GetParent therefore cannot be used to verify adoption at all.
    const HWND parent = GetAncestor(hwnd, GA_PARENT);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    std::printf("  attached : %s (TrySetParent returned non-NULL)\n", attached ? "yes" : "no");
    std::printf("  lastError: %lu\n", static_cast<unsigned long>(attach_error));
    std::printf("  parent   : 0x%p (GetParent says 0x%p)  workerW 0x%p  progman 0x%p\n",
                static_cast<void*>(parent), static_cast<void*>(GetParent(hwnd)),
                static_cast<void*>(layer.worker_w), static_cast<void*>(layer.progman));
    std::printf("  root     : 0x%p\n", static_cast<void*>(root));
    std::printf("  style    : 0x%08lx exstyle=0x%08lx\n",
                static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_STYLE)),
                static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));

    // The verdict: adopted iff the parent chain reaches one of the two desktop
    // windows this layer resolved.
    const bool adopted = parent == layer.progman || parent == layer.worker_w;
    std::printf("  adopted  : %s\n", adopted ? "yes (parent is the desktop)" : "NO");
    for (const auto& line : layer.diagnostics)
        std::printf("  %s\n", line.c_str());

    // Pump messages so the paint actually happens while it is on screen.
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(seconds * 1000);
    while (GetTickCount() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    DestroyWindow(hwnd);
    std::printf("destroyed.\n");
    return adopted ? 0 : 8;
#endif
}

int run_set(const SetOptions& options) {
    if (!fs::exists(options.path)) {
        std::fprintf(stderr, "error: no such file or directory: %s\n", options.path.c_str());
        return 2;
    }

    // Resolve the target display the way the core does: the requested device id,
    // else the primary.
    const DisplayManager manager = make_display_manager();
    if (manager.displays().empty()) {
        std::fprintf(stderr, "error: no displays reported by the OS.\n");
        return 3;
    }
    models::DisplayMonitor target = manager.display_from_point(0, 0).value_or(
        manager.primary_display().value_or(manager.displays().front()));
    if (const auto primary = manager.primary_display())
        target = *primary;
    if (options.screen_device_id) {
        const auto& all = manager.displays();
        const auto found = std::find_if(all.begin(), all.end(),
                                        [&](const models::DisplayMonitor& d) {
                                            return d.device_id == *options.screen_device_id;
                                        });
        if (found == all.end()) {
            std::fprintf(stderr, "error: no display with device id %s\n",
                         options.screen_device_id->c_str());
            return 4;
        }
        target = *found;
    }

    // Type: explicit flag, then the folder's LivelyInfo.json, then the extension.
    models::WallpaperType type = models::WallpaperType::video;
    if (options.type) {
        const auto parsed = parse_type(*options.type);
        if (!parsed) {
            std::fprintf(stderr, "error: unknown --type=%s\n", options.type->c_str());
            return 5;
        }
        type = *parsed;
    } else {
        // get_metadata throws when LivelyInfo.json is absent or malformed, which is
        // the ordinary case for a bare media file — so the exception *is* the
        // "fall back to the extension" signal here.
        bool resolved = false;
        try {
            const common::WallpaperLibraryFactory factory;
            type = factory.get_metadata(options.path).type;
            resolved = true;
        } catch (...) {
            // Not a wallpaper folder; fall through to the extension rule.
        }
        if (!resolved) {
            const std::string extension = lower(fs::path(options.path).extension().string());
            if (extension == ".jpg" || extension == ".png" || extension == ".jpeg" ||
                extension == ".bmp" || extension == ".webp")
                type = models::WallpaperType::picture;
        }
    }

    if (type == models::WallpaperType::picture) {
        // A picture is not a window at all: Windows renders it. There is nothing
        // to adopt, which is why this branch completes immediately.
        PictureWallpaper wallpaper(options.path, models::LibraryModel{}, target,
                                   options.arrangement, options.scaling);
        std::printf("setting picture wallpaper on %s (scaling=%d)\n",
                    target.device_name.c_str(), static_cast<int>(options.scaling));
        try {
            wallpaper.show();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "error: %s\n", ex.what());
            return 6;
        }
        std::printf("done.\n");
        return 0;
    }

    // A process-backed wallpaper. The C# resolves the bundled player and the
    // per-screen LivelyProperties copy before constructing the host; the
    // diagnostic binary does the same with the defaults the settings model holds.
    const std::string base = app_base_directory().string();
    const std::string property_copy =
        (options.type && *options.type == "web")
            ? (fs::path(base) / "plugins" / "mpv" / "LivelyProperties.json").string()
            : (fs::path(base) / "plugins" / "mpv" / "LivelyProperties.json").string();

    MpvWallpaper wallpaper(options.path, models::LibraryModel{}, target, property_copy, base,
                           options.hw_decode);
    std::printf("launching mpv host (pid will follow)\n");
    std::printf("  ipc pipe : %s\n", wallpaper.ipc_server_name().c_str());

    bool loaded = false;
    wallpaper.loaded.subscribe([&] { loaded = true; });
    bool exited = false;
    wallpaper.exited.subscribe([&] { exited = true; });

    try {
        wallpaper.show();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 7;
    }
    std::printf("  pid      : %d\n", wallpaper.pid());
#ifdef _WIN32
    std::printf("  window   : 0x%p\n", static_cast<void*>(wallpaper.handle()));
#endif
    std::printf("  loaded   : %s\n", loaded ? "yes" : "no");

    if (options.adopt) {
        const DesktopLayer layer = setup_desktop_layer();
        // The core's rule: span arrangement puts one window across the whole
        // desktop, everything else parents it to its own monitor's rectangle.
        const bool attached = options.arrangement == models::WallpaperArrangement::span
                                  ? layer.set_wallpaper_span_screen(wallpaper.handle())
                                  : layer.set_wallpaper_per_screen(wallpaper.handle(), target);
        std::printf("  adopted  : %s (workerW 0x%p)\n", attached ? "yes" : "no",
                    static_cast<void*>(layer.worker_w));
        for (const auto& line : layer.diagnostics)
            std::printf("  %s\n", line.c_str());
        if (!attached) {
            std::fprintf(stderr, "error: failed to parent the wallpaper onto the desktop.\n");
            wallpaper.terminate();
            return 8;
        }
    } else {
        std::printf("  adopted  : skipped (--no-adopt)\n");
    }

    // Run until the deadline, the player exits, or the user interrupts. The real
    // app keeps running here serving gRPC and watching for display changes.
    const auto deadline = options.seconds
                              ? std::optional(std::chrono::steady_clock::time_point(
                                    std::chrono::steady_clock::now() +
                                    std::chrono::seconds(*options.seconds)))
                              : std::nullopt;
    std::printf("\nwallpaper running — press Ctrl+C to stop\n");
    while (!exited) {
        if (deadline && std::chrono::steady_clock::now() >= *deadline)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!exited) {
        std::printf("\nclosing...\n");
        wallpaper.close();
        // give mpv a moment to quit through IPC before the hard kill
        for (int i = 0; i < 20 && !exited; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        wallpaper.terminate();
    }
    std::printf("done.\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The picture wallpaper path calls COM, and the mpv path uses
    // GetWindowRect/SetWindowPos on handles owned by another process. STA is what
    // the C# WPF app runs in and what the shell's COM objects expect.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return 0;
    }

    if (args[0] == "workerw")
        return run_workerw();

    if (args[0] == "adopt-test") {
        bool span = false;
        bool classic = false;
        int seconds = 5;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--span")
                span = true;
            else if (args[i] == "--classic")
                classic = true;
            else if (args[i].rfind("--seconds=", 0) == 0)
                seconds = std::atoi(args[i].substr(10).c_str());
        }
        return run_adopt_test(span, seconds, classic);
    }

    if (args[0] != "set") {
        std::fprintf(stderr, "error: unknown command '%s'\n\n", args[0].c_str());
        print_usage();
        return 2;
    }

    SetOptions options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            if (options.path.empty()) {
                options.path = arg;
                continue;
            }
            std::fprintf(stderr, "error: unexpected argument '%s'\n", arg.c_str());
            return 2;
        }
        const auto equals = arg.find('=');
        const std::string name = arg.substr(2, equals == std::string::npos ? std::string::npos
                                                                          : equals - 2);
        const std::string value = equals == std::string::npos ? "" : arg.substr(equals + 1);
        if (name == "no-hwdec") {
            options.hw_decode = false;
        } else if (name == "no-adopt") {
            options.adopt = false;
        } else if (name == "type") {
            options.type = value;
        } else if (name == "screen") {
            options.screen_device_id = value;
        } else if (name == "scaling") {
            const auto parsed = parse_scaler(value);
            if (!parsed) {
                std::fprintf(stderr, "error: unknown --scaling=%s\n", value.c_str());
                return 2;
            }
            options.scaling = *parsed;
        } else if (name == "arrangement") {
            const auto parsed = parse_arrangement(value);
            if (!parsed) {
                std::fprintf(stderr, "error: unknown --arrangement=%s\n", value.c_str());
                return 2;
            }
            options.arrangement = *parsed;
        } else if (name == "seconds") {
            options.seconds = std::atoi(value.c_str());
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    if (options.path.empty()) {
        std::fprintf(stderr, "error: set needs a wallpaper path\n\n");
        print_usage();
        return 2;
    }

    return run_set(options);
}
