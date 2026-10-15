// skinview.cpp - see skinview.h.
#include "skinview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platformfactory.h"

using namespace VSTGUI;
using vti::cc::SkinNode;

namespace tirevive {

static CColor rgba(uint32_t c)
{
    return CColor((uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

SkinView::SkinView(const CRect &size, const vti::cc::SkinLayout &layout, const vti::AccessFiles &files,
                   SkinState &state)
    : CView(size), layout_(layout), files_(files), state_(state)
{
}

CBitmap *SkinView::bitmap(const std::string &name)
{
    if (name.empty())
        return nullptr;
    auto it = bitmaps_.find(name);
    if (it != bitmaps_.end())
        return it->second;
    SharedPointer<CBitmap> b;
    auto &d = files_.get(name);
    if (!d.empty())
        if (auto pb = getPlatformFactory().createBitmapFromMemory(d.data(), (uint32_t)d.size()))
            b = makeOwned<CBitmap>(pb);
    bitmaps_[name] = b;
    return b;
}

void SkinView::tick()
{
    phase_++;
    invalid(); // the pulsing OK button; a full repaint of 687 x 463 is cheap
}

void SkinView::draw(CDrawContext *ctx)
{
    ctx->setFillColor(CColor(0, 0, 0, 255));
    ctx->drawRect(getViewSize(), kDrawFilled);
    draw_nodes(ctx, layout_.nodes);
    setDirty(false);
}

void SkinView::draw_nodes(CDrawContext *ctx, const std::vector<SkinNode> &nodes)
{
    for (auto &n : nodes) {
        if (!n.expression.empty() && n.container && !state_.visible(n.expression))
            continue;
        if (!n.container) {
            draw_control(ctx, n);
            continue;
        }
        if ((n.back & 0xff) != 0) {
            ctx->setFillColor(rgba(n.back));
            ctx->drawRect(CRect(n.x, n.y, n.x + n.w, n.y + n.h), kDrawFilled);
        }
        if (auto *b = bitmap(n.background))
            b->draw(ctx, CRect(n.x, n.y, n.x + b->getWidth(), n.y + b->getHeight()));
        draw_nodes(ctx, n.children);
    }
}

static std::string pressed_name(const std::string &bmp) // "BTN_Backup.png" -> "BTN_Backup_PRS.png"
{
    size_t dot = bmp.rfind('.');
    return dot == std::string::npos ? bmp : bmp.substr(0, dot) + "_PRS" + bmp.substr(dot);
}

void SkinView::draw_control(CDrawContext *ctx, const SkinNode &n)
{
    if (n.type == "menu" || n.type == "patchbrowser") // Burn Flash only: not offered
        return;
    if (!n.expression.empty() && n.type != "animbutton" && !state_.visible(n.expression))
        return;
    bool on = state_.enabled(n);
    float alpha = on ? 1.f : 0.4f;
    CRect r(n.x, n.y, n.x + n.w, n.y + n.h);
    bool down = pressed_ == &n;
    if (n.type == "textlabel") {
        draw_text(ctx, n, state_.text(n.parameter), alpha);
        return;
    }
    if (n.type == "animation") {
        if (auto *b = bitmap(n.bitmap))
            b->draw(ctx, r, CPoint(0, state_.value(n.parameter) * n.h), alpha);
        return;
    }
    if (n.type == "animbutton") {
        auto *b = bitmap(n.bitmap);
        if (down && bitmap(n.background)) {
            bitmap(n.background)->draw(ctx, r, CPoint(0, n.h));
            return;
        }
        if (!b)
            return;
        int frames = (int)(b->getHeight() / n.h), f = 0;
        if (state_.ready(n) && frames > 1) { // pulse up and down
            int p = phase_ % (2 * frames - 2);
            f = p < frames ? p : 2 * frames - 2 - p;
        }
        b->draw(ctx, r, CPoint(0, f * n.h), state_.ready(n) ? 1.f : alpha);
        return;
    }
    // radio / button: two frames
    bool selected = n.type == "radio" ? state_.value(n.parameter) == n.value : down;
    CBitmap *b = down ? bitmap(pressed_name(n.bitmap)) : nullptr;
    if (!b)
        b = bitmap(n.bitmap);
    if (b)
        b->draw(ctx, r, CPoint(0, selected && b->getHeight() >= 2 * n.h ? n.h : 0), alpha);
}

void SkinView::draw_text(CDrawContext *ctx, const SkinNode &n, const std::string &text, float alpha)
{
    auto *font = bitmap(n.bitmap);
    if (!font || text.empty())
        return;
    const int gw = 6, gh = 9, glyphs = (int)(font->getHeight() / gh);
    int x = n.x, line = 0;
    for (char ch : text) {
        if (ch == '\n') { // several lines in a taller label
            line++, x = n.x;
            continue;
        }
        if (x + gw > n.x + n.w)
            continue;
        int g = (unsigned char)ch - 32;
        if (g >= 0 && g < glyphs && (line + 1) * gh <= std::max(n.h, gh))
            font->draw(ctx, CRect(x, n.y + line * (gh + 1), x + gw, n.y + line * (gh + 1) + gh), CPoint(0, g * gh),
                       alpha);
        x += gw;
    }
}

const SkinNode *SkinView::hit(const std::vector<SkinNode> &nodes, int x, int y)
{
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) { // topmost first
        auto &n = *it;
        if (!n.expression.empty() && (n.container || n.type != "animbutton") && !state_.visible(n.expression))
            continue;
        bool inside = x >= n.x && x < n.x + n.w && y >= n.y && y < n.y + n.h;
        if (n.container) {
            if (inside)
                if (auto *h = hit(n.children, x, y))
                    return h;
            continue;
        }
        if (inside && (n.type == "radio" || n.type == "button" || n.type == "animbutton") && state_.enabled(n))
            return &n;
    }
    return nullptr;
}

void SkinView::onMouseDownEvent(MouseDownEvent &event)
{
    pressed_ = hit(layout_.nodes, (int)event.mousePosition.x, (int)event.mousePosition.y);
    if (pressed_)
        invalid();
    event.consumed = true;
}

void SkinView::onMouseUpEvent(MouseUpEvent &event)
{
    auto *p = pressed_;
    pressed_ = nullptr;
    invalid();
    event.consumed = true;
    if (p && hit(layout_.nodes, (int)event.mousePosition.x, (int)event.mousePosition.y) == p)
        state_.clicked(*p);
}

} // namespace tirevive
