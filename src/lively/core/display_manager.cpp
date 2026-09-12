#include <lively/core/display_manager.h>

#include <lively/common/link_util.h>

#include <algorithm>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lively::core {

namespace {

// DisplayManager.PRIMARY_MONITOR — a sentinel meaning "the single-monitor case",
// not a real monitor handle.
constexpr std::intptr_t kPrimaryMonitorSentinel = static_cast<std::intptr_t>(0xBAADF00D);
constexpr std::uint32_t kMonitorInfoPrimary = 0x00000001;
constexpr std::uint32_t kMonitorDefaultToNearest = 0x00000002;
constexpr const char* kDefaultDisplayDeviceName = "DISPLAY";

std::string trim_trailing_nulls(const std::string& value) {
    std::size_t end = value.size();
    while (end > 0 && value[end - 1] == '\0')
        --end;
    return value.substr(0, end);
}

} // namespace

bool is_remote_session() {
#ifdef _WIN32
    constexpr int kSmRemoteSession = 0x1000;
    return GetSystemMetrics(kSmRemoteSession) != 0;
#else
    return false;
#endif
}

std::string default_display_device_id(const models::Rectangle& bounds) {
    // C#: $"{X}-{Y}-{Width}-{Height}" then SHA-256 over the UTF-8 bytes, rendered
    // as uppercase hex with '-' separators removed and lowercased.
    const std::string bounds_string = std::to_string(bounds.x) + "-" +
                                      std::to_string(bounds.y) + "-" +
                                      std::to_string(bounds.width) + "-" +
                                      std::to_string(bounds.height);
    const std::string hash = common::sha256_hex(bounds_string);
    const std::string prefix = is_remote_session() ? "\\\\?\\DISPLAY#REMOTEDISPLAY#"
                                                   : "\\\\?\\DISPLAY#LOCALDISPLAY#";
    return prefix + hash;
}

#ifdef _WIN32

namespace {

models::Rectangle make_rect(int left, int top, int right, int bottom) {
    models::Rectangle rect;
    rect.x = left;
    rect.y = top;
    rect.width = right - left;
    rect.height = bottom - top;
    return rect;
}

// GetDisplayDevice: the first display device attached to the desktop that is not a
// mirroring driver. `device_name` is the "\\.\DISPLAY1" monitor name.
struct DisplayDeviceInfo {
    std::string device_id;
    std::string device_string;
    bool found = false;
};

DisplayDeviceInfo get_display_device(const std::string& device_name) {
    DisplayDeviceInfo result;
    DISPLAY_DEVICEA device{};
    device.cb = sizeof(device);
    // EDD_GET_DEVICE_INTERFACE_NAME = 0x1 — without it DeviceID is the legacy
    // name rather than the device interface path the core persists.
    constexpr DWORD kEddGetDeviceInterfaceName = 0x1;
    // AttachedToDesktop = 0x1, MirroringDriver = 0x8.
    constexpr DWORD kAttachedToDesktop = 0x1;
    constexpr DWORD kMirroringDriver = 0x8;

    for (DWORD id = 0; EnumDisplayDevicesA(device_name.c_str(), id, &device, kEddGetDeviceInterfaceName);
         ++id) {
        const DWORD flags = device.StateFlags;
        if ((flags & kAttachedToDesktop) != 0 && (flags & kMirroringDriver) == 0) {
            result.device_id = device.DeviceID;
            result.device_string = device.DeviceString;
            result.found = true;
            break;
        }
        device.cb = sizeof(device);
    }
    return result;
}

std::vector<HMONITOR> get_h_monitors(bool multi_monitor_support) {
    std::vector<HMONITOR> monitors;
    if (!multi_monitor_support)
        return monitors;   // caller substitutes the primary sentinel

    EnumDisplayMonitors(nullptr, nullptr,
                        [](HMONITOR monitor, HDC, LPRECT, LPARAM lparam) -> BOOL {
                            reinterpret_cast<std::vector<HMONITOR>*>(lparam)->push_back(monitor);
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

models::Rectangle get_working_area() {
    RECT rect{};
    constexpr UINT kSpiGetWorkArea = 0x0030;
    SystemParametersInfoA(kSpiGetWorkArea, 0, &rect, 0);
    return make_rect(rect.left, rect.top, rect.right, rect.bottom);
}

// The virtual screen metrics, not a union of monitor bounds: they are what the C#
// reads, and they differ for a display that is attached but disabled.
models::Rectangle virtual_screen_from_metrics() {
    constexpr int kSmXVirtualScreen = 76;
    constexpr int kSmYVirtualScreen = 77;
    constexpr int kSmCxVirtualScreen = 78;
    constexpr int kSmCyVirtualScreen = 79;
    return make_rect(GetSystemMetrics(kSmXVirtualScreen), GetSystemMetrics(kSmYVirtualScreen),
                     GetSystemMetrics(kSmXVirtualScreen) + GetSystemMetrics(kSmCxVirtualScreen),
                     GetSystemMetrics(kSmYVirtualScreen) + GetSystemMetrics(kSmCyVirtualScreen));
}

} // namespace

void DisplayManager::refresh() {
    constexpr int kSmCMonitors = 80;
    const bool multi_monitor_support = GetSystemMetrics(kSmCMonitors) != 0;

    const std::vector<HMONITOR> h_monitors = get_h_monitors(multi_monitor_support);

    // Every existing entry is marked stale; anything still stale at the end is
    // removed. This is how a display that disappeared is noticed.
    for (auto& display : displays_)
        display.is_stale = true;

    const std::size_t count = multi_monitor_support ? h_monitors.size() : 1u;
    for (std::size_t i = 0; i < count; ++i) {
        const bool primary_branch = !multi_monitor_support ||
                                    (h_monitors.empty() && i == 0);

        if (primary_branch) {
            // Single-monitor path: no multimon call at all, geometry comes from the
            // virtual screen metrics and the device id is synthesised from them.
            auto* existing = [&]() -> models::DisplayMonitor* {
                for (auto& display : displays_) {
                    if (display.device_name == kDefaultDisplayDeviceName)
                        return &display;
                }
                return nullptr;
            }();

            if (existing == nullptr) {
                models::DisplayMonitor created;
                created.device_name = kDefaultDisplayDeviceName;
                displays_.push_back(std::move(created));
                existing = &displays_.back();
            }

            existing->bounds = virtual_screen_from_metrics();
            existing->device_id.clear();
            existing->display_name = "Display";
            existing->h_monitor = static_cast<std::int64_t>(kPrimaryMonitorSentinel);
            existing->is_primary = true;
            existing->working_area = get_working_area();
            existing->is_stale = false;
            existing->index = static_cast<int>(i) + 1;
            continue;
        }

        MONITORINFOEXA info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoA(h_monitors[i], reinterpret_cast<MONITORINFO*>(&info)))
            continue;

        const std::string device_name = trim_trailing_nulls(info.szDevice);

        models::DisplayMonitor* target = nullptr;
        for (auto& display : displays_) {
            if (display.device_name == device_name) {
                target = &display;
                break;
            }
        }
        if (target == nullptr) {
            models::DisplayMonitor created;
            created.device_name = device_name;
            const DisplayDeviceInfo device = get_display_device(device_name);
            if (device.found) {
                created.device_id = device.device_id;
                created.display_name = device.device_string;
            }
            displays_.push_back(std::move(created));
            target = &displays_.back();
        }

        target->h_monitor = static_cast<std::int64_t>(
            reinterpret_cast<std::intptr_t>(h_monitors[i]));
        target->bounds = make_rect(info.rcMonitor.left, info.rcMonitor.top,
                                   info.rcMonitor.right, info.rcMonitor.bottom);
        target->is_primary = (info.dwFlags & kMonitorInfoPrimary) != 0;
        target->working_area = make_rect(info.rcWork.left, info.rcWork.top,
                                         info.rcWork.right, info.rcWork.bottom);
        target->is_stale = false;
        target->index = static_cast<int>(i) + 1;

        // The device-path-less fallback: without it the display has no identity and
        // every comparison against a persisted layout entry fails.
        if (target->device_id.find_first_not_of(" \t\r\n") == std::string::npos)
            target->device_id = default_display_device_id(target->bounds);
    }

    displays_.erase(std::remove_if(displays_.begin(), displays_.end(),
                                   [](const models::DisplayMonitor& display) {
                                       return display.is_stale;
                                   }),
                    displays_.end());
}

std::optional<models::DisplayMonitor> DisplayManager::display_from_window(void* hwnd) const {
    constexpr int kSmCMonitors = 80;
    const bool multi_monitor_support = GetSystemMetrics(kSmCMonitors) != 0;
    const HMONITOR monitor = multi_monitor_support
                                 ? MonitorFromWindow(static_cast<HWND>(hwnd), kMonitorDefaultToNearest)
                                 : reinterpret_cast<HMONITOR>(kPrimaryMonitorSentinel);
    for (const auto& display : displays_) {
        if (display.h_monitor == static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(monitor)))
            return display;
    }
    return std::nullopt;
}

std::optional<models::DisplayMonitor> DisplayManager::display_from_point(int x, int y) const {
    constexpr int kSmCMonitors = 80;
    const bool multi_monitor_support = GetSystemMetrics(kSmCMonitors) != 0;
    POINT point{x, y};
    const HMONITOR monitor = multi_monitor_support
                                 ? MonitorFromPoint(point, kMonitorDefaultToNearest)
                                 : reinterpret_cast<HMONITOR>(kPrimaryMonitorSentinel);
    for (const auto& display : displays_) {
        if (display.h_monitor == static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(monitor)))
            return display;
    }
    return std::nullopt;
}

#else   // !_WIN32

void DisplayManager::refresh() {}
std::optional<models::DisplayMonitor> DisplayManager::display_from_window(void*) const { return std::nullopt; }
std::optional<models::DisplayMonitor> DisplayManager::display_from_point(int, int) const { return std::nullopt; }

#endif

models::Rectangle DisplayManager::virtual_screen_bounds() const {
    // The C# caches this alongside the monitor list; recomputing is the same value
    // because both come from the same metrics call.
#ifdef _WIN32
    return virtual_screen_from_metrics();
#else
    models::Rectangle rect;
    for (const auto& display : displays_) {
        if (rect.width == 0 && rect.height == 0) {
            rect = display.bounds;
            continue;
        }
        const int right = (std::max)(rect.x + rect.width, display.bounds.x + display.bounds.width);
        const int bottom = (std::max)(rect.y + rect.height, display.bounds.y + display.bounds.height);
        rect.x = (std::min)(rect.x, display.bounds.x);
        rect.y = (std::min)(rect.y, display.bounds.y);
        rect.width = right - rect.x;
        rect.height = bottom - rect.y;
    }
    return rect;
#endif
}

std::optional<models::DisplayMonitor> DisplayManager::primary_display() const {
    for (const auto& display : displays_) {
        if (display.is_primary)
            return display;
    }
    return std::nullopt;
}

bool DisplayManager::screen_exists(const models::DisplayMonitor& display) const {
    return std::any_of(displays_.begin(), displays_.end(),
                       [&display](const models::DisplayMonitor& candidate) {
                           return models::same_display(candidate, display);
                       });
}

} // namespace lively::core
