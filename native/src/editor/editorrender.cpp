// editorrender.cpp - see editorrender.h.
#include "editorrender.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vti {

static int frame_of(int v, int lo, int hi, int frames)
{
    if (hi <= lo || frames < 2)
        return 0;
    int f = (int)std::lround((double)(v - lo) / (hi - lo) * (frames - 1));
    return std::max(0, std::min(frames - 1, f));
}

std::string EditorRenderer::value_text(const std::string &param, int v) const
{
    const Parameter *p = cond_.find(param);
    if (!p)
        return std::to_string(v);
    for (const RemoteDevice *d : {&dev_, &ed_}) {
        auto vl = d->valuelists.find(p->valuelist);
        if (vl != d->valuelists.end()) // an existing entry wins, even with empty text (live displays stay blank)
            for (auto &en : vl->second.entries)
                if (en.index == v)
                    return en.text;
    }
    return std::to_string(v);
}

// bitmap font: vertical strip of 96 glyphs (ASCII 32..127), 6 pixels wide
void EditorRenderer::text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h,
                          const std::string &align, double alpha)
{
    void *f = c.image(font);
    if (!f || str.empty())
        return;
    const int gw = 6, gh = c.image_height(f) / 96;
    int tw = (int)str.size() * gw;
    int tx = align.find("Left") != std::string::npos    ? x
             : align.find("Right") != std::string::npos ? x + w - tw
                                                        : x + (w - tw) / 2;
    int ty = align.find("Top") != std::string::npos      ? y
             : align.find("Bottom") != std::string::npos ? y + h - gh
                                                         : y + (h - gh - 1) / 2;
    for (size_t i = 0; i < str.size(); i++) {
        int ch = (unsigned char)str[i];
        if (ch < 32 || ch > 127)
            ch = '?';
        // glyph rectangle clipped to the element
        int gx = tx + (int)i * gw, x0 = std::max(gx, x), x1 = std::min(gx + gw, x + w);
        int y0 = std::max(ty, y), y1 = std::min(ty + gh, y + h);
        if (x1 > x0 && y1 > y0)
            c.blit(f, x0 - gx, (ch - 32) * gh + (y0 - ty), x0, y0, x1 - x0, y1 - y0, alpha);
    }
}

void EditorRenderer::image(Canvas &c, const std::string &name, void *img, int sy, int fh, int x, int y, int w, int h,
                           double alpha)
{
    auto t = layout_.tiles.find(name);
    int iw = c.image_width(img);
    if (t == layout_.tiles.end() || (iw == w && fh == h)) {
        c.blit(img, 0, sy, x, y, std::min(w, iw), std::min(h, fh), alpha);
        return;
    }
    const auto &in = t->second;
    // source columns/rows: [0,l) [l, iw-r) [iw-r, iw); destination middle filled by repeating the source middle
    int sx[3] = {0, in.left, iw - in.right}, sw[3] = {in.left, iw - in.left - in.right, in.right};
    int syy[3] = {0, in.top, fh - in.bottom}, sh[3] = {in.top, fh - in.top - in.bottom, in.bottom};
    int dx[3] = {x, x + in.left, x + w - in.right}, dw[3] = {in.left, w - in.left - in.right, in.right};
    int dy[3] = {y, y + in.top, y + h - in.bottom}, dh[3] = {in.top, h - in.top - in.bottom, in.bottom};
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) {
            if (sw[k] <= 0 || sh[r] <= 0 || dw[k] <= 0 || dh[r] <= 0)
                continue;
            for (int oy = 0; oy < dh[r]; oy += sh[r])
                for (int ox = 0; ox < dw[k]; ox += sw[k])
                    c.blit(img, sx[k], sy + syy[r], dx[k] + ox, dy[r] + oy, std::min(sw[k], dw[k] - ox),
                           std::min(sh[r], dh[r] - oy), alpha);
        }
}

// 4 x 5 pixel digits for the browser's program numbers (drawn in code by the original)
void EditorRenderer::digits(Canvas &c, int number, int right, int top)
{
    static const char *glyph[10][5] = {
        {".##.", "#..#", "#..#", "#..#", ".##."}, {".##.", "#.#.", "..#.", "..#.", "..#."},
        {"###.", "...#", ".##.", "#...", "####"}, {"###.", "...#", ".##.", "...#", "###."},
        {"..##", ".#.#", "#..#", "####", "...#"}, {"####", "#...", "###.", "...#", "###."},
        {".###", "#...", "###.", "#..#", ".##."}, {"####", "...#", "..#.", ".#..", ".#.."},
        {".##.", "#..#", ".##.", "#..#", ".##."}, {".##.", "#..#", ".###", "...#", "###."}};
    std::string s = std::to_string(number);
    int x = right - (int)s.size() * 5 + 1;
    for (char ch : s) {
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 4; k++)
                if (glyph[ch - '0'][r][k] == '#')
                    c.fill(x + k, top + r, 1, 1, Rgba{59, 59, 59, 255});
        x += 5;
    }
}

// the bank table of the browser page: 8 columns x 16 rows (program number + name), measured on the original
void EditorRenderer::patch_browser(Canvas &c, const Element &e, int x, int y, int bank)
{
    const std::string &font = e.get("ControlFont");
    for (int p = 0; p < 128; p++) {
        int nx = x + 21 + (p / 16) * 84, ny = y + 1 + (p % 16) * 11;
        digits(c, p, nx - 5, ny + 4);
        auto sel = selected.find(bank);
        bool on = sel != selected.end() && sel->second == p && !e.get("SelectedFont").empty();
        text(c, on ? e.get("SelectedFont") : font, bank_patch(bank, p), nx, ny, 60, 9, "Top Left");
    }
}

// ADSTR envelope display: stage geometry measured on the original (init patch; the original builds its stages in
// the control factory, not yet decompiled): x0 = 4, top = 3, bottom = h - 4 inside the element, 0.47 px per value
// unit for attack/decay/release, a hold segment of 62 px at sustain 127 .. 94 px at sustain 0, a ~29 px sustain-time
// segment (flat at 64), gradient fill under the curve, line in ForeColor, 7x7 handles, a marker at the slope end.
void EditorRenderer::envelope(Canvas &c, const Element &e, int x, int y, int part, double alpha)
{
    const std::string &first = e.get("Parameter"); // "<Envelope>/Attack"
    std::string g = first.substr(0, first.rfind('/') + 1);
    int A = values(g + "Attack", part), D = values(g + "Decay", part), S = values(g + "Sustain", part);
    int T = values(g + "Sustain Time", part), R = values(g + "Release", part);
    const double k = 0.47, top = y + 3, bottom = y + e.h - 4, h = bottom - top;
    double x0 = x + 4, x1 = x0 + A * k, x2 = x1 + 62 * std::pow(D / 127.0, 1.07), x3 = x2 + 62 + (127 - S) * 0.252;
    double x4 = x3 + 29, x5 = x4 + R * 0.457;
    double ys = bottom - S / 127.0 * h;
    double yt = std::max(top, std::min(bottom, ys - (T - 64) / 63.0 * h * 0.5)); // slope: up for T > 64
    // curve y for every column x0..x5
    auto curve = [&](double px) {
        if (px <= x1)
            return x1 > x0 ? bottom - (px - x0) / (x1 - x0) * h : top;
        if (px <= x2) {
            double t = (px - x1) / std::max(1.0, x2 - x1);
            return ys + (top - ys) * std::pow(1 - t, 3.0);
        }
        if (px <= x3)
            return ys;
        if (px <= x4)
            return ys + (yt - ys) * (px - x3) / (x4 - x3);
        double t = (px - x4) / std::max(1.0, x5 - x4);
        return bottom + (yt - bottom) * std::pow(1 - t, 3.0);
    };
    int cx0 = (int)std::floor(x0), cx1 = (int)std::ceil(x5);
    // gradient fill under the curve, row by row
    for (int row = (int)top; row < (int)bottom; row++) {
        int a = -1, b = -1;
        for (int px = cx0; px <= cx1; px++)
            if (curve(px) <= row) {
                if (a < 0)
                    a = px;
                b = px;
            }
        if (a < 0)
            continue;
        double f = (row - top) / h;
        Rgba col{(uint8_t)(133 + (38 - 133) * f), (uint8_t)(147 + (42 - 147) * f), (uint8_t)(167 + (48 - 167) * f),
                 (uint8_t)(255 * alpha)};
        c.fill(a, row, b - a + 1, 1, col);
    }
    Rgba fc = e.fore;
    fc.a = (uint8_t)(255 * alpha);
    double py = bottom;
    for (int px = cx0; px <= cx1; px++) {
        double cy = curve(px);
        c.line(px - 1 + 0.5, py + 0.5, px + 0.5, cy + 0.5, fc);
        py = cy;
    }
    c.line(x4 + 0.5, top + 0.5, x4 + 0.5, bottom + 0.5, fc); // slope end marker
    if (void *hd = c.image(e.get("NormalHandle"))) {
        int hw = c.image_width(hd), hh = c.image_height(hd);
        double pts[5][2] = {{x1, top}, {x2, ys}, {x3, ys}, {x4, yt}, {x5, bottom}};
        for (auto &pt : pts)
            c.blit(hd, 0, 0, (int)std::lround(pt[0]) - hw / 2, (int)std::lround(pt[1]) - hh / 2, hw, hh, alpha);
    }
}

// filter response displays (LP4/HP4/BP4/BS4Response): measured on the original - line (0,25,65) at the base level
// h/2 - 3, handle (NormalHandle) at x = 3 + cutoff/127 * (w - 17), slope 3 px per px; resonance lifts the handle
void EditorRenderer::filter_response(Canvas &c, const Element &e, int x, int y, int part)
{
    const std::string &cut = e.get("Parameter"); // "Filter N/Cutoff"
    std::string res = cut.substr(0, cut.rfind('/') + 1) + "Resonance";
    int cv = values(cut, part), rv = values(res, part);
    const Rgba col{0, 25, 65, 255};
    double left = x + 3, right = x + e.w - 3, top = y + 4, bottom = y + e.h - 1;
    double base = y + e.h / 2 - 3, hx = x + 3 + cv / 127.0 * (e.w - 17), hy = base - rv / 127.0 * (base - top - 6);
    double drop = (bottom - base) / 3;
    auto ln = [&](double a, double b, double c2, double d) { c.line(a + 0.5, b + 0.5, c2 + 0.5, d + 0.5, col); };
    if (e.type == "LP4Response") {
        ln(left, base, hx - 4, base), ln(hx - 4, base, hx, hy), ln(hx, hy, std::min(right, hx + drop), bottom);
    } else if (e.type == "HP4Response") {
        ln(std::max(left, hx - drop), bottom, hx, hy), ln(hx, hy, hx + 4, base), ln(hx + 4, base, right, base);
    } else if (e.type == "BP4Response") {
        ln(std::max(left, hx - drop), bottom, hx, hy), ln(hx, hy, std::min(right, hx + drop), bottom);
    } else { // BS4Response: flat with a notch at the cutoff
        ln(left, base, hx - drop / 2, base), ln(hx - drop / 2, base, hx, bottom), ln(hx, bottom, hx + drop / 2, base);
        ln(hx + drop / 2, base, right, base);
    }
    if (void *hd = c.image(e.get("NormalHandle")))
        c.blit(hd, 0, 0, (int)std::lround(hx) - c.image_width(hd) / 2, (int)std::lround(hy) - c.image_height(hd) / 2,
               c.image_width(hd), c.image_height(hd), 1.0);
}

void EditorRenderer::render(Canvas &c)
{
    for (auto &e : layout_.elements)
        draw(c, e, 0, 0, -1);
}

void EditorRenderer::draw(Canvas &c, const Element &e, int ox, int oy, int inherited)
{
    // Part -1 = the enclosing patch container's part, else the current part
    int pt = e.part() >= 0 ? e.part() : inherited >= 0 ? inherited : part;
    if (!cond_.eval(e.get("Condition"), values, pt))
        return;
    int x = ox + e.x, y = oy + e.y;
    const std::string &param = e.get("Parameter");
    const Parameter *p = param.empty() ? nullptr : cond_.find(param);
    int v = param.empty() ? 0 : values(param, pt);
    // inactive controls (the parameter's own condition is false; soft knobs without destination) are translucent
    double alpha = 1.0;
    if (p && !p->condition.empty() && !cond_.eval(p->condition, values, pt))
        alpha = disabled_alpha;
    if (param.rfind("Softknobs/Softknob ", 0) == 0 && values("Soft Knobs/Destination " + param.substr(19), pt) == 0)
        alpha = disabled_alpha;
    int lo = p ? p->min : 0, hi = p ? p->max : 127;

    void *normal = c.image(e.get("NormalBack"));
    if (&e == pressed && c.image(e.get("PressedBack")))
        normal = c.image(e.get("PressedBack"));
    if (normal && e.w > 0 && e.h > 0) {
        int frames = std::max(1, c.image_height(normal) / e.h), index = 0;
        if (e.type == "VUSlider") { // level strip (lit for the current part up to its volume) + handle
            void *hd = c.image(e.get("NormalHandle"));
            int hw = hd ? c.image_width(hd) : 0;
            int pos = hi > lo ? (int)std::lround((double)(v - lo) / (hi - lo) * (e.w - hw)) : 0;
            c.blit(normal, 0, (pt == part ? frame_of(v, lo, hi, frames) : 0) * e.h, x, y, e.w, e.h, alpha);
            if (hd)
                c.blit(hd, 0, 0, x + pos, y + (e.h - c.image_height(hd)) / 2, hw, c.image_height(hd), alpha);
        } else {
            if (e.type.find("Knob") != std::string::npos || e.type.find("Slider") != std::string::npos)
                index = frame_of(v, lo, hi, frames);
            else if (e.type == "RadioButton") // 2-frame strips: 0 = off, 1 = selected
                index = frames >= 2 && v == atoi(e.get("OnValue").c_str()) ? 1 : 0;
            else if (e.type == "Button" || e.type == "KickButton")
                index = frames >= 2 && v ? 1 : 0;
            const std::string &nm = (&e == pressed && c.image(e.get("PressedBack"))) ? e.get("PressedBack") : e.get("NormalBack");
            image(c, nm, normal, index * std::min(e.h, c.image_height(normal)), std::min(e.h, c.image_height(normal)), x, y,
                  e.w, e.h, alpha);
        }
    } else if (e.get("Transparent") == "false" && e.back.a)
        c.fill(x, y, e.w, e.h, e.back);

    if (e.type.size() == 11 && e.type.compare(3, 8, "Response") == 0 && !param.empty())
        filter_response(c, e, x, y, pt);
    if (e.type == "ADSTREnvelope" && !param.empty())
        envelope(c, e, x, y, pt, alpha);
    if (e.type == "PatchBrowser" && bank_patch && !param.empty()) {
        // "Browser N/Selected Bank" (internal to the original): browser N shows RAM bank N by default
        int bank = values(param, pt);
        if (bank == cond_.default_value(param) && !cond_.find(param))
            bank = param.size() > 8 ? param[8] - '0' : 0;
        patch_browser(c, e, x, y, bank);
    }
    const std::string &font = e.get("ControlFont");
    if (!font.empty() && e.type != "PatchBrowser") {
        std::string str;
        if (e.type == "StaticText")
            str = e.get("Name");
        else if (e.type == "NameEdit" && patch_name)
            str = patch_name(pt);
        else if ((e.type == "TextSlider" || e.type == "Menu") && !param.empty()) {
            str = value_text(param, v);
            if (e.type == "Menu") // valuelists indent entries for the popup (" Mod Wheel"); the control shows them trimmed
                str.erase(0, str.find_first_not_of(' ') == std::string::npos ? str.size() : str.find_first_not_of(' '));
        }
        int tl = 0, tr = 0;
        auto t = layout_.tiles.find(e.get("NormalBack"));
        if (t != layout_.tiles.end())
            tl = t->second.left, tr = t->second.right;
        text(c, font, str, x + tl, y, e.w - tl - tr, e.h, e.get("Align"), alpha);
    }
    for (auto &ch : e.children)
        draw(c, ch, x, y, e.part() >= 0 ? e.part() : inherited);
}

bool EditorRenderer::hit(const Element &e, int ox, int oy, int inherited, int px, int py, Hit &out) const
{
    int pt = e.part() >= 0 ? e.part() : inherited >= 0 ? inherited : part;
    if (!cond_.eval(e.get("Condition"), values, pt))
        return false;
    int x = ox + e.x, y = oy + e.y;
    bool inside = px >= x && px < x + e.w && py >= y && py < y + e.h;
    if (inside && !e.get("Parameter").empty() && e.get("Enabled") != "false") {
        out.element = &e; // later (= drawn on top) elements override
        out.x = x, out.y = y, out.part = pt;
    }
    for (auto &ch : e.children)
        hit(ch, x, y, e.part() >= 0 ? e.part() : inherited, px, py, out);
    return out.element != nullptr;
}

int EditorRenderer::browser_entry(const Hit &h, int x, int y)
{
    if (!h.element || h.element->type != "PatchBrowser")
        return -1;
    int col = (x - h.x - 21 + 30) / 84, row = (y - h.y - 1) / 11; // entry = digits (left) + 60 px name
    if (col < 0 || col > 7 || row < 0 || row > 15)
        return -1;
    return col * 16 + row;
}

Hit EditorRenderer::hit_test(int px, int py) const
{
    Hit h;
    for (auto &e : layout_.elements)
        hit(e, 0, 0, -1, px, py, h);
    return h;
}

} // namespace vti
