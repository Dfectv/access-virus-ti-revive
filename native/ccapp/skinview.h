// skinview.h - draws the layout of Access' Virus Control Center (src/cc/ccskin.h) with its own images, loaded at
// runtime from the user's installer, and reports clicks. What each element shows and does is up to a SkinState
// (the app's logic); this view only knows the drawing conventions of the original's images:
//   - two-frame images (height = 2 x element): top = normal / not selected, bottom = pressed / selected;
//     an "<name>_PRS.png" next to it is the pressed look while the mouse is down
//   - animbutton: an animation strip (frames of the element's height), pulsing while it is ready; its
//     `background` image is the pressed look
//   - animation: one frame of a strip, chosen by the state
//   - textlabel: the LCD bitmap font (6 x 9 pixel glyphs, ASCII 32..127, in one column)
#pragma once
#include "access/accessfiles.h"
#include "cc/ccskin.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cpoint.h"
#include <map>
#include <string>

namespace tirevive {

class SkinState {
public:
    virtual ~SkinState() = default;
    // is a container / control with this `expression` shown (empty expression: always)
    virtual bool visible(const std::string &expression) = 0;
    // radio: selected value of the parameter; animation: frame
    virtual int value(const std::string &parameter) = 0;
    virtual std::string text(const std::string &parameter) = 0;
    // can it be clicked (disabled elements are drawn dimmed)
    virtual bool enabled(const vti::cc::SkinNode &n) = 0;
    // animbutton: ready = pulsing
    virtual bool ready(const vti::cc::SkinNode &n) = 0;
    virtual void clicked(const vti::cc::SkinNode &n) = 0;
    virtual void right_click(VSTGUI::CPoint where) { (void)where; } // not on a control
};

class SkinView : public VSTGUI::CView {
public:
    SkinView(const VSTGUI::CRect &size, const vti::cc::SkinLayout &layout, const vti::AccessFiles &files,
             SkinState &state);
    void draw(VSTGUI::CDrawContext *ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent &event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent &event) override;
    void tick(); // call ~20 times a second: animations

private:
    const vti::cc::SkinLayout &layout_;
    const vti::AccessFiles &files_;
    SkinState &state_;
    std::map<std::string, VSTGUI::SharedPointer<VSTGUI::CBitmap>> bitmaps_;
    const vti::cc::SkinNode *pressed_ = nullptr;
    int phase_ = 0;

    VSTGUI::CBitmap *bitmap(const std::string &name);
    void draw_nodes(VSTGUI::CDrawContext *ctx, const std::vector<vti::cc::SkinNode> &nodes);
    void draw_control(VSTGUI::CDrawContext *ctx, const vti::cc::SkinNode &n);
    void draw_text(VSTGUI::CDrawContext *ctx, const vti::cc::SkinNode &n, const std::string &text, float alpha);
    const vti::cc::SkinNode *hit(const std::vector<vti::cc::SkinNode> &nodes, int x, int y);
};

} // namespace tirevive
