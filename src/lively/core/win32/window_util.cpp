#include <lively/core/win32/window_util.h>

#ifdef _WIN32
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace lively::core::win32 {

namespace {

constexpr int kMaxClassNameChars = 256;

bool is_empty_rect(const models::Rectangle& r) {
    return r.width <= 0 || r.height <= 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Pure geometry

bool is_window_covering_target(const models::Rectangle& window_rect,
                               const models::Rectangle& target_area,
                               double threshold) {
    // C#: Rectangle.Intersect(windowRect, targetArea) then ratio >= threshold.
    const int left = (std::max)(window_rect.x, target_area.x);
    const int top = (std::max)(window_rect.y, target_area.y);
    const int right = (std::min)(window_rect.x + window_rect.width,
                                 target_area.x + target_area.width);
    const int bottom = (std::min)(window_rect.y + window_rect.height,
                                  target_area.y + target_area.height);

    // The C# guard: `if (!(right >= left && bottom >= top)) return false;`
    if (!(right >= left && bottom >= top))
        return false;

    const std::int64_t intersection_width = (std::max)(0, right - left);
    const std::int64_t intersection_height = (std::max)(0, bottom - top);
    const std::int64_t intersection_area = intersection_width * intersection_height;

    const std::int64_t target_size =
        static_cast<std::int64_t>(target_area.width) * target_area.height;
    if (target_size <= 0)
        return false;   // C# would divide by zero here

    const double coverage_ratio = static_cast<double>(intersection_area) /
                                  static_cast<double>(target_size);
    return coverage_ratio >= threshold;
}

bool is_display_covered_by_window_grid(const std::vector<models::Rectangle>& window_rects,
                                       bool any_window_maximized,
                                       const models::Rectangle& screen_bounds,
                                       int tile_size,
                                       double threshold) {
    if (window_rects.empty())
        return false;

    // C#: `if (topLevelWindows.Exists(IsZoomed)) return true;` — a single
    // maximized window means covered, regardless of geometry.
    if (any_window_maximized)
        return true;

    const int width = screen_bounds.width;
    const int height = screen_bounds.height;
    if (width <= 0 || height <= 0 || tile_size <= 0)
        return false;

    const int cols = static_cast<int>(std::ceil(static_cast<double>(width) / tile_size));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(height) / tile_size));
    const int total_tiles = rows * cols;
    if (total_tiles <= 0)
        return false;

    std::vector<bool> covered(static_cast<std::size_t>(total_tiles), false);
    int covered_count = 0;

    for (const auto& rect : window_rects) {
        // Win32 GetWindowRect on a dead handle returns 0, which the C# treats as
        // "skip" — the same shape as an empty rect here.
        if (is_empty_rect(rect))
            continue;

        if (is_window_covering_target(rect, screen_bounds, 0.95))
            return true;

        const int x_start = (std::max)(0, (rect.x - screen_bounds.x) / tile_size);
        const int x_end = (std::min)(cols - 1,
            (rect.x + rect.width - screen_bounds.x - 1) / tile_size);
        const int y_start = (std::max)(0, (rect.y - screen_bounds.y) / tile_size);
        const int y_end = (std::min)(rows - 1,
            (rect.y + rect.height - screen_bounds.y - 1) / tile_size);

        for (int y = y_start; y <= y_end; ++y) {
            for (int x = x_start; x <= x_end; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * cols + x);
                if (!covered[index]) {
                    covered[index] = true;
                    ++covered_count;

                    if (static_cast<double>(total_tiles - covered_count) / total_tiles <= threshold)
                        return true;
                }
            }
        }
    }

    return false;
}

bool is_display_covered_by_any_window(const std::vector<models::Rectangle>& window_rects,
                                      const models::Rectangle& screen_bounds,
                                      double threshold) {
    for (const auto& rect : window_rects) {
        if (is_window_covering_target(rect, screen_bounds, threshold))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Live HWND helpers

#ifdef _WIN32

namespace {

std::int64_t get_window_long_ptr(Hwnd hwnd, int index) {
    return static_cast<std::int64_t>(
        reinterpret_cast<std::intptr_t>(GetWindowLongPtrW(hwnd, index)));
}

void set_window_long_ptr(Hwnd hwnd, int index, std::int64_t value) {
    SetWindowLongPtrW(hwnd, index,
                      static_cast<LONG_PTR>(static_cast<std::intptr_t>(value)));
}

} // namespace

bool try_set_parent(Hwnd child, Hwnd parent) {
    return SetParent(child, parent) != nullptr;
}

Hwnd get_last_child_window(Hwnd parent) {
    Hwnd last_child = nullptr;
    EnumChildWindows(parent, [](HWND hWnd, LPARAM lparam) -> BOOL {
        *reinterpret_cast<HWND*>(lparam) = hWnd;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&last_child));
    return last_child;
}

void set_window_transparency(Hwnd hwnd, std::uint8_t alpha) {
    constexpr int kGWLExStyle = -20;
    constexpr int kLwaAlpha = 0x2;
    const auto ex_style = get_window_long_ptr(hwnd, kGWLExStyle);
    if ((ex_style & kWS_EX_LAYERED) == 0)
        set_window_long_ptr(hwnd, kGWLExStyle, ex_style | kWS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, alpha, kLwaAlpha);
}

void set_window_style(Hwnd hwnd, std::int64_t style_to_add) {
    constexpr int kGWLStyle = -16;
    const auto current = get_window_long_ptr(hwnd, kGWLStyle);
    set_window_long_ptr(hwnd, kGWLStyle, current | style_to_add);
}

void set_window_ex_style(Hwnd hwnd, std::int64_t ex_style_to_add) {
    constexpr int kGWLExStyle = -20;
    const auto current = get_window_long_ptr(hwnd, kGWLExStyle);
    set_window_long_ptr(hwnd, kGWLExStyle, current | ex_style_to_add);
}

bool has_extended_style(Hwnd hwnd, std::uint32_t style) {
    if (hwnd == nullptr)
        return false;
    constexpr int kGWLExStyle = -20;
    return (get_window_long_ptr(hwnd, kGWLExStyle) & style) != 0;
}

std::int64_t get_window_ex_style(Hwnd hwnd) {
    constexpr int kGWLExStyle = -20;
    return get_window_long_ptr(hwnd, kGWLExStyle);
}

std::string get_class_name(Hwnd hwnd) {
    char buffer[kMaxClassNameChars] = {};
    const int length = GetClassNameA(hwnd, buffer, kMaxClassNameChars);
    if (length <= 0)
        return std::string();
    return std::string(buffer, static_cast<std::size_t>(length));
}

bool has_class(Hwnd hwnd, const std::string& expected_class_name) {
    if (hwnd == nullptr)
        return false;
    const std::string actual = get_class_name(hwnd);
    if (actual.size() != expected_class_name.size())
        return false;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        if (lower(actual[i]) != lower(expected_class_name[i]))
            return false;
    }
    return true;
}

void remove_window_from_taskbar(Hwnd hwnd) {
    if (hwnd == nullptr)
        return;
    constexpr int kGWLExStyle = -20;
    const auto current = get_window_long_ptr(hwnd, kGWLExStyle);
    // C#: | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
    const auto updated = current |
        static_cast<std::int64_t>(kWS_EX_NOACTIVATE) |
        static_cast<std::int64_t>(kWS_EX_TOOLWINDOW);
    // The C# hides, mutates, then shows, with the note that cached window data
    // is not applied until the window is re-shown.
    ShowWindow(hwnd, SW_HIDE);
    set_window_long_ptr(hwnd, kGWLExStyle, updated);
    ShowWindow(hwnd, SW_SHOW);
}

void borderless_win_style(Hwnd hwnd) {
    if (hwnd == nullptr)
        return;
    constexpr int kGWLStyle = -16;
    constexpr int kGWLExStyle = -20;

    // WS_CAPTION (0x00C00000) is WS_BORDER|WS_DLGFRAME.
    constexpr std::int64_t kStrip =
        /*WS_CAPTION*/ 0x00C00000LL |
        /*WS_THICKFRAME*/ 0x00040000LL |
        /*WS_SYSMENU*/ 0x00080000LL |
        /*WS_MAXIMIZEBOX*/ 0x00010000LL |
        /*WS_MINIMIZEBOX*/ 0x00020000LL;

    // WS_EX_DLGMODALFRAME | WS_EX_COMPOSITED | WS_EX_WINDOWEDGE |
    // WS_EX_CLIENTEDGE | WS_EX_LAYERED | WS_EX_STATICEDGE |
    // WS_EX_TOOLWINDOW | WS_EX_APPWINDOW
    constexpr std::int64_t kStripEx =
        /*WS_EX_DLGMODALFRAME*/ 0x00000001LL |
        /*WS_EX_WINDOWEDGE*/    0x00000100LL |
        /*WS_EX_CLIENTEDGE*/    0x00000200LL |
        /*WS_EX_STATICEDGE*/    0x00020000LL |
        /*WS_EX_LAYERED*/       0x00080000LL |
        /*WS_EX_TOOLWINDOW*/    0x00000080LL |
        /*WS_EX_APPWINDOW*/     0x00040000LL |
        /*WS_EX_COMPOSITED*/    0x02000000LL;

    const auto style = get_window_long_ptr(hwnd, kGWLStyle);
    const auto ex_style = get_window_long_ptr(hwnd, kGWLExStyle);

    set_window_long_ptr(hwnd, kGWLStyle, style & ~kStrip);
    set_window_long_ptr(hwnd, kGWLExStyle, ex_style & ~kStripEx);

    const HMENU menu = GetMenu(hwnd);
    if (menu != nullptr) {
        constexpr UINT kMfByPosition = 0x00000400;
        constexpr UINT kMfRemove = 0x00001000;
        const int count = GetMenuItemCount(menu);
        for (int i = 0; i < count; ++i)
            RemoveMenu(menu, 0, kMfByPosition | kMfRemove);
        DrawMenuBar(hwnd);
    }
}

bool is_top_level_window(Hwnd hwnd) {
    return GetAncestor(hwnd, GA_ROOT) == hwnd;
}

bool is_cloaked_window(Hwnd hwnd) {
    int cloaked = 0;
    const HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    // The C# ignores the HRESULT and reads the out variable, which stays 0 when
    // the call fails — so a failure means "not cloaked".
    return SUCCEEDED(hr) && cloaked != 0;
}

bool is_visible_top_level_window(Hwnd hwnd) {
    if (hwnd == nullptr)
        return false;
    if (!IsWindowVisible(hwnd))
        return false;
    if (is_cloaked_window(hwnd))
        return false;
    if ((get_window_ex_style(hwnd) & (kWS_EX_LAYERED | kWS_EX_TRANSPARENT)) != 0)
        return false;   // IsTransparentWindow
    if (IsIconic(hwnd))
        return false;
    const auto ex_style = get_window_ex_style(hwnd);
    if ((ex_style & kWS_EX_TOOLWINDOW) != 0)
        return false;
    // WS_EX_NOACTIVATE windows are skipped unless they also carry WS_EX_APPWINDOW.
    if ((ex_style & kWS_EX_NOACTIVATE) != 0 && (ex_style & kWS_EX_APPWINDOW) == 0)
        return false;
    models::Rectangle rect;
    if (!get_window_rect(hwnd, rect))
        return false;
    if (GetWindowTextLengthW(hwnd) == 0)
        return false;
    return is_top_level_window(hwnd);
}

std::vector<Hwnd> get_visible_top_level_windows() {
    std::vector<Hwnd> windows;
    EnumWindows([](HWND hWnd, LPARAM lparam) -> BOOL {
        auto* out = reinterpret_cast<std::vector<Hwnd>*>(lparam);
        if (is_visible_top_level_window(hWnd))
            out->push_back(hWnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

models::Rectangle to_rectangle(int left, int top, int right, int bottom) {
    models::Rectangle rect;
    rect.x = left;
    rect.y = top;
    rect.width = right - left;
    rect.height = bottom - top;
    return rect;
}

bool get_window_rect(Hwnd hwnd, models::Rectangle& out) {
    RECT rect{};
    if (GetWindowRect(hwnd, &rect) == 0)
        return false;
    out = to_rectangle(rect.left, rect.top, rect.right, rect.bottom);
    return true;
}

models::Rectangle get_window_rect(Hwnd hwnd) {
    models::Rectangle rect;
    get_window_rect(hwnd, rect);
    return rect;
}

#else   // !_WIN32

bool try_set_parent(Hwnd, Hwnd) { return false; }
Hwnd get_last_child_window(Hwnd) { return nullptr; }
void set_window_transparency(Hwnd, std::uint8_t) {}
void set_window_style(Hwnd, std::int64_t) {}
void set_window_ex_style(Hwnd, std::int64_t) {}
bool has_extended_style(Hwnd, std::uint32_t) { return false; }
std::int64_t get_window_ex_style(Hwnd) { return 0; }
std::string get_class_name(Hwnd) { return std::string(); }
bool has_class(Hwnd, const std::string&) { return false; }
void remove_window_from_taskbar(Hwnd) {}
void borderless_win_style(Hwnd) {}
bool is_top_level_window(Hwnd) { return false; }
bool is_cloaked_window(Hwnd) { return false; }
bool is_visible_top_level_window(Hwnd) { return false; }
std::vector<Hwnd> get_visible_top_level_windows() { return {}; }

models::Rectangle to_rectangle(int left, int top, int right, int bottom) {
    models::Rectangle rect;
    rect.x = left;
    rect.y = top;
    rect.width = right - left;
    rect.height = bottom - top;
    return rect;
}

bool get_window_rect(Hwnd, models::Rectangle&) { return false; }
models::Rectangle get_window_rect(Hwnd) { return models::Rectangle(); }

#endif

} // namespace lively::core::win32
