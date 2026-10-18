// workarea.h - the work area a popup menu may use: that of the monitor at the menu's anchor. Linux' _NET_WORKAREA is
// one box around all monitors (this PC: 4413 x 1408 over a 2560 x 1440 and a 1920 x 1080 monitor 223 px lower), so a
// menu could open where the smaller monitor has no pixels; per-monitor rects come from mutter's _GTK_WORKAREAS_D<n>
// or from RandR's monitors cut to _NET_WORKAREA. Header only, no platform code: popupwindow_x11.cpp and the tests.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vti {

// native screen coordinates: Windows pixels (as the host's thread sees them), macOS points with y down from the top
// of the main screen, X11 root window pixels
struct NativeRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
};

inline NativeRect intersect(const NativeRect &a, const NativeRect &b)
{
    const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return x1 > x0 && y1 > y0 ? NativeRect{x0, y0, x1 - x0, y1 - y0} : NativeRect{};
}

// the area that contains (px, py), else the nearest one (an anchor on a panel or between monitors), else `fallback`
inline NativeRect work_area_at(const std::vector<NativeRect> &areas, int px, int py, const NativeRect &fallback)
{
    const NativeRect *best = nullptr;
    long long best_d = 0;
    for (const auto &a : areas) {
        if (a.empty())
            continue;
        if (a.contains(px, py))
            return a;
        const long long dx = std::max({a.x - px, 0, px - (a.x + a.w - 1)});
        const long long dy = std::max({a.y - py, 0, py - (a.y + a.h - 1)});
        const long long d = dx * dx + dy * dy;
        if (!best || d < best_d)
            best = &a, best_d = d;
    }
    return best ? *best : fallback;
}

// a CARDINAL list of x, y, w, h groups (_GTK_WORKAREAS_D<n>); empty rects dropped, a cut-off group ignored
inline std::vector<NativeRect> parse_work_areas(const uint32_t *v, size_t n)
{
    std::vector<NativeRect> out;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        NativeRect r{(int)v[i], (int)v[i + 1], (int)v[i + 2], (int)v[i + 3]};
        if (!r.empty())
            out.push_back(r);
    }
    return out;
}

// each monitor cut to the desktop's work area (panels at the monitors' outer edges); a monitor the work area misses,
// or no work area at all, stays whole
inline std::vector<NativeRect> clip_monitors(const std::vector<NativeRect> &monitors, const NativeRect &work)
{
    std::vector<NativeRect> out;
    for (const auto &m : monitors) {
        const NativeRect c = work.empty() ? NativeRect{} : intersect(m, work);
        out.push_back(c.empty() ? m : c);
    }
    return out;
}

} // namespace vti
