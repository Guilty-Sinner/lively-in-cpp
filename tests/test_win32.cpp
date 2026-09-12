// The Win32 half of the core, tested where it can be tested.
//
// Nothing here needs a desktop. The pieces that do (adoption onto WorkerW, the
// mpv child's window, IDesktopWallpaper) are exercised by `lively_core workerw`
// and `lively_core set` on a machine with a desktop — see the README.
//
// What *is* testable is the part that is easy to get wrong and silent when wrong:
//
//   * the coverage predicates the suspend/screensaver logic is built on. The C#
//     uses a 50px tile grid rather than exact union area, and it short-circuits on
//     a maximized window or on any window that alone covers 95% — three separate
//     rules that a "cleaner" implementation would collapse into one and thereby
//     answer differently.
//   * the display identity fallback. A monitor without a device interface path
//     gets `\\?\DISPLAY#LOCALDISPLAY#` + lowercase-hex SHA-256 of
//     "<x>-<y>-<w>-<h>", and the *bounds* string matters: a display that comes
//     back at the same geometry is "the same screen", one that moved is not. The
//     expected digests below were computed independently (python hashlib), so this
//     is a real hash pin and not the port checking itself.
//   * the rect rebasing for a monitor at a negative coordinate, which is the case
//     that breaks a port that assumes the primary monitor starts at 0,0.

#include <catch2/catch_test_macros.hpp>

#include <lively/core/desktop.h>
#include <lively/core/desktop_layout.h>
#include <lively/core/display_manager.h>
#include <lively/core/win32/window_util.h>

#include <string>
#include <vector>

using namespace lively;
using namespace lively::core;

namespace {

models::Rectangle rect(int x, int y, int width, int height) {
    models::Rectangle r;
    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

} // namespace

TEST_CASE("coverage ratio is intersection over target, not over the window", "[win32]") {
    const auto screen = rect(0, 0, 1920, 1080);

    // A window exactly the size of the screen covers it.
    CHECK(win32::is_window_covering_target(screen, screen, 0.95));
    // Half the screen does not.
    CHECK_FALSE(win32::is_window_covering_target(rect(0, 0, 960, 1080), screen, 0.95));
    // 95% exactly: the C# uses `>=`, so this passes.
    CHECK(win32::is_window_covering_target(rect(0, 0, 1920, 1026), screen, 0.95));
    // One pixel short of 95% does not.
    CHECK_FALSE(win32::is_window_covering_target(rect(0, 0, 1920, 1025), screen, 0.95));

    // Off-origin and overlapping: only the intersection counts.
    CHECK(win32::is_window_covering_target(rect(-500, -500, 3000, 3000), screen, 0.95));

    // A window entirely off-screen contributes nothing.
    CHECK_FALSE(win32::is_window_covering_target(rect(5000, 5000, 100, 100), screen, 0.95));

    // Disjoint but "adjacent" rectangles: right < left means no overlap at all.
    CHECK_FALSE(win32::is_window_covering_target(rect(1920, 0, 100, 100), screen, 0.95));

    // A zero-area target would divide by zero in the C#; the port refuses.
    CHECK_FALSE(win32::is_window_covering_target(screen, rect(0, 0, 0, 0), 0.95));
}

TEST_CASE("the grid check's three short-circuits behave like the C#", "[win32]") {
    const auto screen = rect(0, 0, 1920, 1080);

    // No windows at all: not covered (the C# returns false before any math).
    CHECK_FALSE(win32::is_display_covered_by_window_grid({}, false, screen));

    // A maximized window covers the display regardless of its rectangle — the C#
    // checks IsZoomed before it looks at geometry, precisely because a maximized
    // window can have a misleading window rect.
    CHECK(win32::is_display_covered_by_window_grid({rect(0, 0, 10, 10)}, true, screen));

    // A single window alone >= 95% short-circuits.
    CHECK(win32::is_display_covered_by_window_grid({screen}, false, screen));

    // Two windows that tile the screen completely: covered.
    {
        const std::vector<models::Rectangle> halves = {rect(0, 0, 960, 1080), rect(960, 0, 960, 1080)};
        CHECK(win32::is_display_covered_by_window_grid(halves, false, screen));
    }

    // One 960x1080 window leaves half the grid uncovered, which is 50% > the 5%
    // threshold — i.e. NOT covered, even though the window is large.
    CHECK_FALSE(win32::is_display_covered_by_window_grid({rect(0, 0, 960, 1080)}, false, screen));

    // A window whose rectangle is empty (a closed handle: GetWindowRect returns 0)
    // is skipped, and skipping the only window leaves the display uncovered rather
    // than crashing.
    CHECK_FALSE(win32::is_display_covered_by_window_grid({rect(0, 0, 0, 0)}, false, screen));

    // Coverage is quantized to whole tiles. On 1920x1080 the grid is 39x22
    // (ceil(1920/50) x ceil(1080/50)) = 858 tiles, so one uncovered tile row is
    // 39/858 = 4.55% — under the 5% threshold — while two rows are 9.09%, over it.
    // A 1030px-tall window leaves exactly one row and so counts as covered:
    // 50 pixels of missing wallpaper is not enough to call the desktop visible.
    CHECK(win32::is_display_covered_by_window_grid({rect(0, 0, 1920, 1030)}, false, screen));
    CHECK_FALSE(win32::is_display_covered_by_window_grid({rect(0, 0, 1920, 980)}, false, screen));

    // On this geometry the grid and the separate 95%-of-area rule agree, because
    // one tile row is 4.55% and one tile column is 2.56% of the total — both close
    // to the same 5% threshold from below, and a whole multiple of a tile overshoots
    // it. So the boundary cases below are decided by *both* rules, which is worth
    // knowing before changing either: a 1850px-wide window is 96.35% of the screen
    // and a single window covering 36 of 39 columns, so both say covered.
    CHECK(win32::is_display_covered_by_window_grid({rect(0, 0, 1900, 1080)}, false, screen));
    CHECK(win32::is_display_covered_by_window_grid({rect(0, 0, 1850, 1080)}, false, screen));
    CHECK(win32::is_window_covering_target(rect(0, 0, 1850, 1080), screen, 0.95));
    // 1820px is 94.79% of the area and leaves two whole columns (5.13% of the
    // tiles) — the first case where both rules flip to "not covered".
    CHECK_FALSE(win32::is_display_covered_by_window_grid({rect(0, 0, 1820, 1080)}, false, screen));
    CHECK_FALSE(win32::is_window_covering_target(rect(0, 0, 1820, 1080), screen, 0.95));

    // A screen whose size is not a multiple of the tile size still uses ceil() for
    // the grid, so the last row/column are partial tiles that count in full.
    const auto odd = rect(0, 0, 1919, 1079);
    CHECK(win32::is_display_covered_by_window_grid({odd}, false, odd));
    CHECK_FALSE(win32::is_display_covered_by_window_grid({rect(0, 0, 1919, 979)}, false, odd));
}

TEST_CASE("display identity falls back to a hash of the monitor's bounds", "[win32][display]") {
    // `is_remote_session()` decides the prefix, so both accepted values are
    // asserted against the same hashes.
    const std::string local = "\\\\?\\DISPLAY#LOCALDISPLAY#";
    const std::string remote = "\\\\?\\DISPLAY#REMOTEDISPLAY#";

    const std::string a = default_display_device_id(rect(0, 0, 1920, 1080));
    CHECK((a == local + "2cf5abcfa175606a4a5351a983640c4731fca35e3e683755eae0c2cdfac6929d" ||
           a == remote + "2cf5abcfa175606a4a5351a983640c4731fca35e3e683755eae0c2cdfac6929d"));

    // A monitor to the LEFT of the primary: bounds.x is negative and the id must
    // differ from the primary's, because the string it hashes differs.
    const std::string b = default_display_device_id(rect(-1920, 0, 1920, 1080));
    CHECK(b.find("b105917030965a0c345ba7c31d5696d034d8f5dbe9fd4e72237f777263f7cfd0") !=
          std::string::npos);

    // The hash is over "<x>-<y>-<w>-<h>", so identical geometry always yields the
    // same id — that is the property that lets a re-plugged monitor resume its
    // wallpaper.
    CHECK(default_display_device_id(rect(0, 0, 1920, 1080)) == a);
    // A single pixel of difference must not.
    CHECK(default_display_device_id(rect(0, 0, 1920, 1081)) != a);
}

TEST_CASE("per-screen rects are rebased on the parent window, span rects are not", "[win32][desktop]") {
    const auto primary = rect(0, 0, 1920, 1080);
    const auto left = rect(-1920, 0, 1920, 1080);

    // WorkerW at the virtual-screen origin with the union size.
    const auto worker = rect(-1920, 0, 3840, 1080);

    // The display's screen rectangle expressed in the WorkerW's space: a monitor to
    // the left of the virtual origin ends up at x = 1920, NOT at a negative x.
    const auto left_in_parent = screen_to_parent_rect(left, worker);
    CHECK(left_in_parent.x == 0);
    CHECK(left_in_parent.y == 0);
    CHECK(left_in_parent.width == 1920);
    CHECK(left_in_parent.height == 1080);

    const auto primary_in_parent = screen_to_parent_rect(primary, worker);
    CHECK(primary_in_parent.x == 1920);

    // When the WorkerW starts at the origin (the common single-monitor case) nothing
    // moves — and that is the case a port which always rebases would still get
    // right, which is why the negative-origin value above is the one that matters.
    const auto zero_origin_worker = rect(0, 0, 1920, 1080);
    const auto unchanged = screen_to_parent_rect(primary, zero_origin_worker);
    CHECK(unchanged.x == 0);
    CHECK(unchanged.y == 0);

    // The span rect is the WorkerW's own extent at (0,0) — deliberately NOT rebased,
    // which is what makes a span wallpaper cover the whole desktop even when the
    // virtual screen starts at a negative coordinate.
    const auto span = span_rect(worker);
    CHECK(span.x == 0);
    CHECK(span.y == 0);
    CHECK(span.width == 3840);
    CHECK(span.height == 1080);
}

TEST_CASE("IsMultiScreen is a bare count, not a geometry test", "[win32][display]") {
    // The C# is `DisplayMonitors.Count > 1`. This matters: a *single* monitor at a
    // negative origin is still single-screen, so the span branch must not engage,
    // and desktop_layout's own oracle already pins the decision side. Here the
    // count rule is pinned directly.
    DisplayManager manager;
    CHECK_FALSE(manager.is_multi_screen());
    CHECK(manager.displays().empty());
    CHECK(manager.virtual_screen_bounds().width >= 0);

    // The layout helper expresses the same rule over a caller-supplied list.
    const std::vector<models::DisplayMonitor> none;
    CHECK_FALSE(core::is_multi_screen(none));
    std::vector<models::DisplayMonitor> one(1);
    CHECK_FALSE(core::is_multi_screen(one));
    one.emplace_back();
    CHECK(core::is_multi_screen(one));
}
