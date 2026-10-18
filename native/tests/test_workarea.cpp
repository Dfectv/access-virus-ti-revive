// the popup menus' work area on a desktop with several monitors: the one of the monitor at the anchor, not the box
// around all of them (Linux _NET_WORKAREA, round 0.8.26)
#include "check.h"
#include "editor/workarea.h"
using namespace vti;

namespace {
bool same(const NativeRect &a, const NativeRect &b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

// this PC's desktop (GNOME on XWayland): 2560 x 1440 left with the top bar and dock, 1920 x 1080 right, 223 px lower
const NativeRect kLeft{67, 32, 2493, 1408}, kRight{2560, 223, 1920, 1080}, kBox{67, 32, 4413, 1408};
} // namespace

TEST(workarea_monitor_at_anchor)
{
    const std::vector<NativeRect> areas{kLeft, kRight};
    CHECK(same(work_area_at(areas, 1000, 700, kBox), kLeft));
    CHECK(same(work_area_at(areas, 3000, 1200, kBox), kRight));
    CHECK(same(work_area_at(areas, 2560, 223, kBox), kRight)); // the first pixel belongs to it
}

// an anchor outside every area (under the dock, or above the right monitor's top): the nearest one
TEST(workarea_nearest_when_outside)
{
    const std::vector<NativeRect> areas{kLeft, kRight};
    CHECK(same(work_area_at(areas, 20, 700, kBox), kLeft));    // on the dock
    CHECK(same(work_area_at(areas, 3000, 100, kBox), kRight)); // no monitor there: right is 123 px away
    CHECK(same(work_area_at(areas, 3000, 1350, kBox), kRight)); // below the right monitor (no pixels there)
    CHECK(same(work_area_at(areas, 2600, 1350, kBox), kLeft));  // 41 px from the left one, 48 px from the right
}

TEST(workarea_fallback_without_areas)
{
    CHECK(same(work_area_at({}, 3000, 1200, kBox), kBox));
    CHECK(same(work_area_at({NativeRect{0, 0, 0, 0}}, 3000, 1200, kBox), kBox)); // empty rects do not count
}

// _GTK_WORKAREAS_D<n> (mutter): x, y, w, h per monitor
TEST(workarea_gtk_property)
{
    const uint32_t v[] = {67, 32, 2493, 1408, 2560, 223, 1920, 1080};
    auto areas = parse_work_areas(v, 8);
    CHECK(areas.size() == 2 && same(areas[0], kLeft) && same(areas[1], kRight));
    CHECK(parse_work_areas(v, 6).size() == 1); // a cut-off property: whole groups only
    const uint32_t zero[] = {0, 0, 0, 0};
    CHECK(parse_work_areas(zero, 4).empty());
}

// RandR monitors without per-monitor work areas (KDE, Xfce): each monitor cut to _NET_WORKAREA (panels at its edges)
TEST(workarea_monitors_clipped)
{
    const std::vector<NativeRect> monitors{{0, 0, 2560, 1440}, {2560, 223, 1920, 1080}};
    auto areas = clip_monitors(monitors, kBox);
    CHECK(areas.size() == 2 && same(areas[0], kLeft) && same(areas[1], kRight));
    // a work area that misses a monitor (a stale property): keep that monitor whole
    auto other = clip_monitors(monitors, NativeRect{0, 0, 2560, 1440});
    CHECK(other.size() == 2 && same(other[1], monitors[1]));
    // no _NET_WORKAREA: the monitors as they are
    auto plain = clip_monitors(monitors, NativeRect{});
    CHECK(same(plain[0], monitors[0]) && same(plain[1], monitors[1]));
}
