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

    const std::string &font = e.get("ControlFont");
    if (!font.empty()) {
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

Hit EditorRenderer::hit_test(int px, int py) const
{
    Hit h;
    for (auto &e : layout_.elements)
        hit(e, 0, 0, -1, px, py, h);
    return h;
}

} // namespace vti
