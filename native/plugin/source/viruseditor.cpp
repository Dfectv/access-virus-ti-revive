// viruseditor.cpp - see viruseditor.h.
#include "viruseditor.h"
#include "virusplugin.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#if SMTG_OS_LINUX
// server-side (X pixmap) copies of the bitmaps: VSTGUI keeps CPU image surfaces and uploads their pixels on every
// draw into the X back buffer (~67 ms per full repaint on a real X server / XWayland vs ~8 ms on Xvfb). Cairo::Bitmap
// has no way to wrap another surface, so its members are opened up for this file only.
#define private public
#include "vstgui/lib/platform/linux/cairobitmap.h"
#undef private
#include "vstgui/lib/platform/linux/x11platform.h"
#include <cairo/cairo-xcb.h>
#include <xcb/xcb.h>
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/platform/common/genericoptionmenu.h"
#include "vstgui/lib/cfont.h"
#endif

using namespace VSTGUI;

namespace vti {

// Canvas on a VSTGUI draw context; bitmaps are created once from the user's PNGs.
#if SMTG_OS_LINUX
// an X pixmap copy of `bmp` for drawing into the frame's window (server-side composite, GPU with glamor)
static SharedPointer<CBitmap> server_bitmap(CFrame *frame, CBitmap *bmp)
{
    if (!frame || !bmp || getenv("VTI_NATIVE_NO_PIXMAPS"))
        return nullptr;
    auto *cb = dynamic_cast<Cairo::Bitmap *>(bmp->getPlatformBitmap().get());
    if (!cb || !cb->surface || cairo_surface_get_type(cb->surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return nullptr;
    static cairo_surface_t *ref = nullptr; // a 1x1 surface on the frame's window: create_similar makes pixmaps
    if (!ref) {
        auto *conn = X11::RunLoop::instance().getXcbConnection();
        auto win = (xcb_window_t)(uintptr_t)frame->getPlatformFrame()->getPlatformRepresentation();
        if (!conn || !win)
            return nullptr;
        auto *attr = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, win), nullptr);
        if (!attr)
            return nullptr;
        xcb_visualtype_t *vt = nullptr;
        for (auto sc = xcb_setup_roots_iterator(xcb_get_setup(conn)); sc.rem && !vt; xcb_screen_next(&sc))
            for (auto d = xcb_screen_allowed_depths_iterator(sc.data); d.rem && !vt; xcb_depth_next(&d))
                for (auto v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
                    if (v.data->visual_id == attr->visual) {
                        vt = v.data;
                        break;
                    }
        free(attr);
        if (!vt)
            return nullptr;
        ref = cairo_xcb_surface_create(conn, win, vt, 1, 1);
    }
    int w = cairo_image_surface_get_width(cb->surface), h = cairo_image_surface_get_height(cb->surface);
    cairo_surface_t *srv = cairo_surface_create_similar(ref, CAIRO_CONTENT_COLOR_ALPHA, w, h);
    if (cairo_surface_status(srv) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(srv);
        return nullptr;
    }
    cairo_t *cr = cairo_create(srv);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, cb->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(srv);
    auto nb = makeOwned<Cairo::Bitmap>();
    nb->surface = Cairo::SurfaceHandle(srv);
    nb->size = CPoint(w, h);
    nb->scaleFactor = cb->scaleFactor;
    return makeOwned<CBitmap>(nb);
}
#endif

class VstguiCanvas : public Canvas {
public:
    VstguiCanvas(const AccessFiles &files, std::map<std::string, SharedPointer<CBitmap>> &cache)
        : files_(files), cache_(cache)
    {
    }
    CDrawContext *ctx = nullptr;
    CFrame *frame = nullptr; // set: drawing into the window - use the server-side copies (Linux)
    std::map<std::string, SharedPointer<CBitmap>> server_; // X pixmap copies

    void *image(const std::string &name) override
    {
        if (name.empty())
            return nullptr;
        auto it = cache_.find(name);
        if (it == cache_.end()) {
            SharedPointer<CBitmap> bmp;
            const auto &d = files_.get(name);
            if (!d.empty())
                if (auto pb = getPlatformFactory().createBitmapFromMemory(d.data(), (uint32_t)d.size()))
                    bmp = makeOwned<CBitmap>(pb);
            it = cache_.emplace(name, bmp).first;
        }
#if SMTG_OS_LINUX
        if (frame && it->second) {
            auto sv = server_.find(name);
            if (sv == server_.end())
                sv = server_.emplace(name, server_bitmap(frame, it->second.get())).first;
            if (sv->second)
                return sv->second.get();
        }
#endif
        return it->second.get();
    }
    int image_width(void *i) override { return (int)((CBitmap *)i)->getWidth(); }
    int image_height(void *i) override { return (int)((CBitmap *)i)->getHeight(); }
    void blit(void *img, int sx, int sy, int dx, int dy, int w, int h, double alpha) override
    {
        ((CBitmap *)img)->draw(ctx, CRect(dx, dy, dx + w, dy + h), CPoint(sx, sy), (float)alpha);
    }
    void fill(int x, int y, int w, int h, Rgba c) override
    {
        ctx->setFillColor(CColor(c.r, c.g, c.b, c.a));
        ctx->drawRect(CRect(x, y, x + w, y + h), kDrawFilled);
    }
    void line(double x0, double y0, double x1, double y1, Rgba c) override
    {
        ctx->setFrameColor(CColor(c.r, c.g, c.b, c.a));
        ctx->setLineWidth(1);
        ctx->drawLine(CPoint(x0, y0), CPoint(x1, y1));
    }

private:
    const AccessFiles &files_;
    std::map<std::string, SharedPointer<CBitmap>> &cache_;
};

class EditorCanvasView : public CView, public IControlListener, public TextEditListenerAdapter {
public:
    EditorCanvasView(VirusPlugin *plugin, const CRect &r)
        : CView(r), plugin_(plugin), renderer_(plugin->layout(), plugin->conditions(), plugin->device(), plugin->editor_device()),
          canvas_(plugin->files(), bitmaps_)
    {
        renderer_.values = [this](const std::string &path, int part) { return plugin_->ui_value(path, part); };
        renderer_.patch_name = [this](int part) { return plugin_->patch_name(part); };
        renderer_.bank_patch = [this](int bank, int prog) { return plugin_->bank_patch(bank, prog); };
        renderer_.live_text = [this](const std::string &p) { return plugin_->live_text(p); };
        renderer_.meter = [this](int ch) { return plugin_->meter(ch); };
        // device changes (knobs on the Virus, automation) repaint the view
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) {
            plugin_->diag_report();
            if (plugin_->ui_changed() && !partial_) { // during a drag the drag repaints its own area
                plugin_->diag.dirty_ticks++;
                plugin_->lcd_changed();
                invalid();
            } else if (plugin_->lcd_changed() && !partial_)
                invalidRect(CRect(200, 470, 560, 512)); // the LCD only
            else if (int changed = plugin_->tick_meters(2)) { // the original: -1 per ~25 ms idle tick; 2 per 50 ms
                // repaint only the changed meters, merged into one area (a repaint per meter made hosts sluggish)
                if (meter_rects_.empty()) { // the VU sliders (one per part), top to bottom = part 1..16
                    for (auto &rc : renderer_.param_rects("Internal MIDI Standard Parameters/07 Volume"))
                        meter_rects_.push_back(CRect(rc[0], rc[1], rc[0] + rc[2], rc[1] + rc[3]));
                    std::sort(meter_rects_.begin(), meter_rects_.end(),
                              [](const CRect &a, const CRect &b) { return a.top < b.top; });
                }
                CRect area;
                for (int ch = 0; ch < 16 && ch < (int)meter_rects_.size(); ch++)
                    if (changed & (1 << ch)) {
                        if (area.isEmpty())
                            area = meter_rects_[ch];
                        else
                            area.unite(meter_rects_[ch]);
                    }
                if (!area.isEmpty())
                    invalidRect(area);
            }
        }, 50);
    }

    void draw(CDrawContext *ctx) override { drawRect(ctx, getViewSize()); }

    void drawRect(CDrawContext *ctx, const CRect &update) override
    {
        renderer_.part = plugin_->ui_value(plugin_->layout().frame_parameter, -1);
        canvas_.ctx = ctx;
        CRect r(update);
        r.offset(-getViewSize().left, -getViewSize().top);
        renderer_.clip_x0 = (int)std::floor(r.left), renderer_.clip_y0 = (int)std::floor(r.top);
        renderer_.clip_x1 = (int)std::ceil(r.right), renderer_.clip_y1 = (int)std::ceil(r.bottom);
        auto t0 = std::chrono::steady_clock::now();
        // the static layer (backgrounds, panels, labels) lives in an offscreen bitmap, rebuilt only when the
        // visible structure changes (page, oscillator mode, ...); every repaint copies it and draws the controls
        // the structure can only change with full repaints (page switch, menus, state); partial ones (knob drags,
        // meters, LCD) reuse the cache without walking the conditions
        bool full_update = r.getWidth() >= getViewSize().getWidth() - 1 && r.getHeight() >= getViewSize().getHeight() - 1;
        std::vector<const Element *> sig;
        if (!static_bg_ || full_update)
            sig = renderer_.static_signature();
        if (!static_bg_ || (full_update && sig != static_key_)) {
            CRect full = getViewSize();
            if (auto off = COffscreenContext::create(CPoint(full.getWidth(), full.getHeight()))) {
                off->beginDraw();
                canvas_.ctx = off;
                canvas_.frame = nullptr; // CPU images into the CPU offscreen
                int c0 = renderer_.clip_x0, c1 = renderer_.clip_y0, c2 = renderer_.clip_x1, c3 = renderer_.clip_y1;
                renderer_.clip_x0 = renderer_.clip_y0 = -100000;
                renderer_.clip_x1 = renderer_.clip_y1 = 100000;
                renderer_.layer = 1;
                renderer_.render(canvas_);
                renderer_.clip_x0 = c0, renderer_.clip_y0 = c1, renderer_.clip_x1 = c2, renderer_.clip_y1 = c3;
                off->endDraw();
                static_bg_ = off->getBitmap();
#if SMTG_OS_LINUX
                if (auto sb = server_bitmap(getFrame(), static_bg_))
                    static_bg_ = sb; // the cache as an X pixmap too
#endif
                static_key_ = sig;
                canvas_.ctx = ctx;
                if (getenv("VTI_NATIVE_DRAWTIME"))
                    fprintf(stderr, "static layer rebuilt\n");
            }
        }
        canvas_.frame = getFrame(); // into the window: server-side bitmaps (Linux)
        if (static_bg_ && full_update) { // small areas: drawing their few elements directly is cheaper on X11
            CRect dst(update);
            static_bg_->draw(ctx, dst, CPoint(dst.left - getViewSize().left, dst.top - getViewSize().top));
            renderer_.layer = 2;
        } else
            renderer_.layer = 0;
        renderer_.render(canvas_);
        {
            long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            plugin_->diag.draw_us += us;
            (r.getWidth() >= getViewSize().getWidth() - 1 ? plugin_->diag.full_draws : plugin_->diag.part_draws)++;
        }
        if (getenv("VTI_NATIVE_DRAWTIME"))
            fprintf(stderr, "editor draw %dx%d: %.2f ms\n", (int)r.getWidth(), (int)r.getHeight(),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (getenv("VTI_NATIVE_DEBUG")) {
            int ok = 0, missing = 0;
            for (auto &b : bitmaps_)
                (b.second ? ok : missing)++;
            fprintf(stderr, "editor draw: %d bitmaps, %d missing\n", ok, missing);
        }
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent &event) override
    {
        Hit h = renderer_.hit_test((int)event.mousePosition.x, (int)event.mousePosition.y);
        if (getenv("VTI_NATIVE_DEBUG"))
            fprintf(stderr, "mouse down %.0f,%.0f -> %s %s\n", event.mousePosition.x, event.mousePosition.y,
                    h.element ? h.element->type.c_str() : "-", h.element ? h.element->get("Parameter").c_str() : "");
        if (!h.element)
            return;
        const Element &e = *h.element;
        const std::string &param = e.get("Parameter");
        if (!param.empty() && !renderer_.param_active(param, h.part)) { // greyed out: locked like in the original
            event.consumed = true;
            return;
        }
        // knobs and sliders: Ctrl+click or double click = back to the parameter's default value
        if ((e.type.find("Knob") != std::string::npos || e.type.find("Slider") != std::string::npos) &&
            (event.modifiers.has(ModifierKey::Control) || event.clickCount >= 2)) {
            plugin_->ui_set(param, h.part, plugin_->conditions().default_value(param));
            invalid();
            event.consumed = true;
            return;
        }
        drag_ = h;
        plugin_->take_changed(); // start collecting the changes of this drag
        drag_y_ = event.mousePosition.y;
        drag_x_ = event.mousePosition.x;
        dnd_ = false, dnd_prog_ = -1;
        drag_value_ = plugin_->ui_value(param, h.part);
        if (e.type == "NameEdit" && event.clickCount >= 2) { // rename the part's patch
            edit_name(h);
            event.consumed = true;
            return;
        }
        if (e.type == "ArpEdit") { // user pattern: drag a bar's right edge = length, else click/drag = velocity
            arp_length_drag_ = -1;
            int step = std::max(0, std::min(31, (int)(event.mousePosition.x - h.x - 1) / 20));
            std::string st = "Arpeggiator User Pattern/Step " + std::to_string(step + 1) + " ";
            if (plugin_->ui_value(st + "Bitfield", h.part)) {
                int cx, bottom;
                EditorRenderer::arp_step_geometry(e, h.x, h.y, step, cx, bottom);
                int r = EditorRenderer::arp_step_width(plugin_->ui_value(st + "Length", h.part));
                if (std::abs(event.mousePosition.x - (cx + r)) <= 2)
                    arp_length_drag_ = step;
            }
            if (arp_length_drag_ < 0)
                arp_set_velocity(h, event.mousePosition);
            event.consumed = true;
            return;
        }
        if (e.type == "PatchBrowser") { // click selects, double click loads into the current part
            int prog = EditorRenderer::browser_entry(h, (int)event.mousePosition.x, (int)event.mousePosition.y);
            int bank = plugin_->ui_value(e.get("Parameter"), -1);
            if (prog >= 0) {
                dnd_bank_ = bank, dnd_prog_ = prog; // may be dragged onto a part strip
                renderer_.selected[bank] = prog;
                if (event.clickCount >= 2)
                    plugin_->load_patch(bank, prog, plugin_->ui_value(plugin_->layout().frame_parameter, -1));
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (e.type == "IncButton" || e.type == "DecButton") { // step the value (browser banks, categories ...)
            int lo = 0, hi = plugin_->bank_count() - 1;
            if (const Parameter *p = plugin_->conditions().find(param))
                lo = p->min, hi = p->max;
            else if (param == "Repository Actions/Category Filter")
                hi = 22;
            if (drag_value_ == VirusPlugin::SEARCH_BANK) { // the arrows page through the search results
                plugin_->results_step(e.type == "IncButton" ? 1 : -1);
                renderer_.pressed = &e;
                invalid();
                event.consumed = true;
                return;
            }
            int v = drag_value_ + (e.type == "IncButton" ? 1 : -1);
            plugin_->ui_set(param, h.part, std::max(lo, std::min(hi, v)));
            renderer_.pressed = &e;
            invalid();
            event.consumed = true;
            return;
        }
        if (e.type == "SearchEdit" || (e.type == "RadioButton" && param == "Repository Actions/Search")) {
            edit_search(h);
            event.consumed = true;
            return;
        }
        if (e.type == "Menu" || e.type == "HierarchicMenu" || e.type == "PatchSaveMenu") {
            if (param == "Repository Actions/Current Patch" || param == "Repository Actions/Current Patch Save Action")
                patch_menu(h, event.mousePosition, param == "Repository Actions/Current Patch Save Action");
            else if (param.rfind("Browser ", 0) == 0)
                bank_menu(h, event.mousePosition);
            else if (param == "Repository Actions/Category Filter")
                category_menu(h, event.mousePosition);
            else
                popup_menu(h, event.mousePosition);
            event.consumed = true;
            return;
        }
        if (e.type == "RadioButton")
            plugin_->ui_set(param, h.part, atoi(e.get("OnValue").c_str()));
        else if (e.type == "Button" || e.type == "KickButton")
            plugin_->ui_set(param, h.part, drag_value_ ? 0 : 1);
        renderer_.pressed = &e;
        invalid();
        event.consumed = true;
    }

    void onMouseMoveEvent(MouseMoveEvent &event) override
    {
        if (!drag_.element || !event.buttonState.isLeft())
            return;
        const Element &e = *drag_.element;
        if (e.type == "ArpEdit") {
            if (arp_length_drag_ >= 0) { // length from the right edge position (inverse of the width table)
                int cx, bottom;
                EditorRenderer::arp_step_geometry(e, drag_.x, drag_.y, arp_length_drag_, cx, bottom);
                int want = (int)event.mousePosition.x - cx, best = 0;
                for (int len = 0; len <= 127; len++)
                    if (std::abs(EditorRenderer::arp_step_width(len) - want) <
                        std::abs(EditorRenderer::arp_step_width(best) - want))
                        best = len;
                plugin_->ui_set("Arpeggiator User Pattern/Step " + std::to_string(arp_length_drag_ + 1) + " Length",
                                drag_.part, best);
            } else
                arp_set_velocity(drag_, event.mousePosition);
            invalid();
            event.consumed = true;
            return;
        }
        // drag and drop like the original: a part's patch name (or a browser entry) onto another part strip
        if (e.type == "NameEdit" || (e.type == "PatchBrowser" && dnd_prog_ >= 0)) {
            if (!dnd_ && std::hypot(event.mousePosition.x - drag_x_, event.mousePosition.y - drag_y_) > 4) {
                dnd_ = true;
                if (auto *f = getFrame())
                    f->setCursor(kCursorCopy);
            }
            event.consumed = true;
            return;
        }
        if (e.type.find("Knob") == std::string::npos && e.type.find("Slider") == std::string::npos)
            return;
        const std::string &param = e.get("Parameter");
        const Parameter *p = plugin_->conditions().find(param);
        int lo = p ? p->min : 0, hi = p ? p->max : 127;
        // knobs and text sliders: full range over 200 pixels upwards, like a VSTGUI knob; sliders follow the mouse
        // 1:1 along their track (the handle stays under the cursor)
        double delta = (drag_y_ - event.mousePosition.y) * (hi - lo) / 200.0;
        if (e.type == "HorizontalSlider" || e.type == "VUSlider") {
            int hw = 0;
            if (void *hd = canvas_.image(e.get("NormalHandle")))
                hw = canvas_.image_width(hd);
            delta = (event.mousePosition.x - drag_x_) * (hi - lo) / std::max(1.0, (double)(e.w - hw));
        } else if (e.type == "VerticalSlider") {
            int hh = 0;
            if (void *hd = canvas_.image(e.get("NormalHandle")))
                hh = canvas_.image_height(hd);
            delta = (drag_y_ - event.mousePosition.y) * (hi - lo) / std::max(1.0, (double)(e.h - hh));
        }
        int v = (int)std::lround(drag_value_ + delta);
        plugin_->ui_set(param, drag_.part, std::max(lo, std::min(hi, v)));
        // repaint only the control and the LCD (bottom info panel); value-dependent layouts change on release
        if (drag_rects_.empty())
            drag_rects_ = renderer_.param_rects(param);
        for (auto &rc : drag_rects_) // every control of this parameter (knob + value display ...)
            invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
        for (auto &other : plugin_->take_changed()) // linked parameters (LFO/filter links) move along
            if (other != param)
                for (auto &rc : renderer_.param_rects(other))
                    invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
        invalidRect(CRect(200, 470, 560, 512));
        partial_ = true;
        event.consumed = true;
    }

    void onMouseUpEvent(MouseUpEvent &event) override
    {
        if (dnd_ && drag_.element) { // dropped: onto a part strip (the part panel, x < 192)?
            if (auto *f = getFrame())
                f->setCursor(kCursorDefault);
            Hit t = renderer_.hit_test((int)event.mousePosition.x, (int)event.mousePosition.y);
            bool strip = t.element && event.mousePosition.x < 192 && t.part >= 0 && t.part < 16;
            if (strip && drag_.element->type == "NameEdit")
                plugin_->copy_part(drag_.part, t.part);
            else if (strip && dnd_prog_ >= 0)
                plugin_->load_patch(dnd_bank_, dnd_prog_, t.part);
            dnd_ = false;
        }
        drag_ = {};
        drag_rects_.clear();
        renderer_.pressed = nullptr;
        partial_ = false;
        invalid();
        event.consumed = true;
    }

    // IControlListener: the name edit was committed
    void valueChanged(CControl *c) override
    {
        if (auto *te = dynamic_cast<CTextEdit *>(c)) {
            std::string t = te->getText().getString();
            if (editing_search_) { // live search: every change filters; the field goes away with the focus
                plugin_->set_search(t);
                invalid();
                return;
            }
            plugin_->rename_part(edit_part_, t);
            if (auto *f = getFrame()) // remove the edit field after the current event
                f->doAfterEventProcessing([f, te]() { f->removeView(te); });
            invalid();
        }
    }

    // ITextEditListener: the search field lost the focus (return, click elsewhere)
    void onTextEditPlatformControlLostFocus(CTextEdit *te) override
    {
        te->unregisterTextEditListener(this);
        editing_search_ = false;
        if (auto *f = getFrame())
            f->doAfterEventProcessing([f, te]() { f->removeView(te); });
        invalid();
    }

private:
    int arp_length_drag_ = -1; // ArpEdit: step whose length is dragged
    // ArpEdit: the step under the mouse gets the velocity of the mouse height (bars grow from the bottom row)
    void arp_set_velocity(const Hit &h, const CPoint &p)
    {
        int step = std::max(0, std::min(31, (int)(p.x - h.x - 1) / 20));
        int cx, bottom;
        EditorRenderer::arp_step_geometry(*h.element, h.x, h.y, step, cx, bottom);
        int vel = std::max(0, std::min(127, bottom - (int)p.y));
        plugin_->ui_set("Arpeggiator User Pattern/Step " + std::to_string(step + 1) + " Velocity", h.part, vel);
        invalid();
    }
    int edit_part_ = 0;
    std::vector<CRect> meter_rects_; // the part strips' meters
    SharedPointer<CBitmap> static_bg_;              // cached static layer
    std::vector<const Element *> static_key_;       // its visible conditional static elements
    bool editing_search_ = false;
    CRect search_rect_;

    // the submenu of a library's folder ("Virus TI2 Factory Backup/Rom/...") inside `root`, created on demand
    static COptionMenu *folder_menu(COptionMenu *root, const std::string &name,
                                    std::map<std::string, COptionMenu *> &folders, std::string &leaf)
    {
        COptionMenu *m = root;
        std::string path;
        size_t from = 0;
        for (size_t slash; (slash = name.find('/', from)) != std::string::npos; from = slash + 1) {
            path = name.substr(0, slash);
            auto it = folders.find(path);
            if (it == folders.end()) {
                auto *sub = new COptionMenu();
                m->addEntry(sub, name.substr(from, slash - from).c_str());
                sub->forget(); // owned by the parent entry
                it = folders.emplace(path, sub).first;
            }
            m = it->second;
        }
        leaf = name.substr(from);
        return m;
    }

    // LOAD: RAM A..D -> patch -> into the part; SAVE: RAM A..D -> slot -> store the part's patch there
    // LOAD / SAVE like the original: "<device> > RAM A.. (SAVE: RAM only) > 000 Name" and "Personal Libraries"
    void patch_menu(const Hit &h, const CPoint &where, bool save)
    {
        auto *menu = new COptionMenu();
        auto *dev = new COptionMenu();
        auto banks = std::make_shared<std::map<COptionMenu *, int>>(); // submenu -> bank
        for (int b = 0; b < (save ? 4 : plugin_->bank_count()); b++) {
            auto *bm = new COptionMenu();
            for (int p = 0; p < 128; p++) {
                char t[40];
                snprintf(t, sizeof t, "%03d %s", p, plugin_->bank_patch(b, p).c_str());
                bm->addEntry(t);
            }
            dev->addEntry(bm, VirusPlugin::bank_name(b));
            (*banks)[bm] = b;
            bm->forget();
        }
        menu->addEntry(dev, plugin_->device_name());
        dev->forget();
        auto *lib = new COptionMenu(); // personal libraries (patch files); SAVE can also append
        std::map<std::string, COptionMenu *> folders;
        if (plugin_->library_count() == 0)
            lib->addEntry("(none)", -1, CMenuItem::kDisabled);
        else
            for (int i = 0; i < plugin_->library_count(); i++) {
                std::string leaf;
                COptionMenu *parent = folder_menu(lib, plugin_->library_name(i), folders, leaf);
                auto *lm = new COptionMenu();
                for (int p = 0; p < plugin_->library_size(i); p++) {
                    char t[40];
                    snprintf(t, sizeof t, "%03d %s", p, plugin_->bank_patch(VirusPlugin::LIB_BASE + i, p).c_str());
                    lm->addEntry(t);
                }
                if (save && plugin_->library_size(i) < 128) {
                    char t[40];
                    snprintf(t, sizeof t, "%03d (new)", plugin_->library_size(i));
                    lm->addEntry(t);
                }
                parent->addEntry(lm, leaf.c_str());
                (*banks)[lm] = VirusPlugin::LIB_BASE + i;
                lm->forget();
            }
        menu->addEntry(lib, "Personal Libraries");
        lib->forget();
        int part = h.part;
        menu->popup(getFrame(), where, [this, part, save, banks](COptionMenu *m) {
            int32_t idx = -1;
            COptionMenu *sub = m->getLastItemMenu(idx);
            auto it = banks->find(sub);
            if (it == banks->end() || idx < 0)
                return;
            if (save && it->second >= VirusPlugin::LIB_BASE)
                plugin_->store_to_library(part, it->second - VirusPlugin::LIB_BASE, idx);
            else if (save)
                plugin_->store_patch(part, it->second, idx);
            else
                plugin_->load_patch(it->second, idx, part);
            invalid();
        });
        menu->forget();
    }

    // the browser's bank selector: "<device> > RAM A .. ROM Z" and "Personal Libraries"
    void bank_menu(const Hit &h, const CPoint &where)
    {
        auto *menu = new COptionMenu();
        auto *dev = new COptionMenu();
        for (int b = 0; b < plugin_->bank_count(); b++)
            dev->addEntry(VirusPlugin::bank_name(b));
        menu->addEntry(dev, plugin_->device_name());
        auto *lib = new COptionMenu();
        // (submenu, entry index) -> library; folders become submenus like in the original
        auto entries = std::make_shared<std::map<std::pair<COptionMenu *, int>, int>>();
        std::map<std::string, COptionMenu *> folders;
        if (plugin_->library_count() == 0)
            lib->addEntry("(none)", -1, CMenuItem::kDisabled);
        for (int i = 0; i < plugin_->library_count(); i++) {
            std::string leaf;
            COptionMenu *parent = folder_menu(lib, plugin_->library_name(i), folders, leaf);
            parent->addEntry(leaf.c_str());
            (*entries)[{parent, parent->getNbEntries() - 1}] = i;
        }
        menu->addEntry(lib, "Personal Libraries");
        std::string param = h.element->get("Parameter");
        menu->popup(getFrame(), where, [this, param, dev, entries](COptionMenu *m) {
            int32_t idx = -1;
            COptionMenu *sub = m->getLastItemMenu(idx);
            auto it = entries->find({sub, idx});
            if (sub == dev && idx >= 0)
                plugin_->ui_set(param, -1, idx);
            else if (it != entries->end())
                plugin_->ui_set(param, -1, VirusPlugin::LIB_BASE + it->second);
            invalid();
        });
        lib->forget();
        dev->forget();
        menu->forget();
    }

    void category_menu(const Hit &h, const CPoint &where)
    {
        auto *menu = new COptionMenu(CRect(), nullptr, -1, nullptr, nullptr, COptionMenu::kCheckStyle);
        // the original's list: "All" + the categories in alphabetical order (editordevice "Val Name Category Filter")
        auto vl = plugin_->editor_device().valuelists.find("Val Name Category Filter");
        if (vl != plugin_->editor_device().valuelists.end())
            for (auto &en : vl->second.entries)
                menu->addEntry(en.text.c_str());
        else
            menu->addEntry("All");
        // the generic (Linux) menu ticks the entry of the menu's value
        int cur = plugin_->ui_value("Repository Actions/Category Filter", -1);
        menu->setMax((float)std::max(0, menu->getNbEntries() - 1));
        menu->setValue((float)cur);
        menu->checkEntryAlone(cur);
        menu->popup(getFrame(), where, [this](COptionMenu *m) {
            if (m->getLastResult() >= 0)
                plugin_->ui_set("Repository Actions/Category Filter", -1, m->getLastResult());
            invalid();
        });
        menu->forget();
    }

    void edit_search(const Hit &h)
    {
        auto *f = getFrame();
        if (!f)
            return;
        if (editing_search_)
            return;
        editing_search_ = true;
        // the field of the layout (the magnifier button left of it opens the same field)
        const Element *field = h.element;
        if (field->type != "SearchEdit")
            for (auto &rc : renderer_.param_rects("Repository Actions/Search"))
                if (rc[2] > rc[3] * 2) { // the wide one is the text field
                    search_rect_ = CRect(rc[0], rc[1], rc[0] + rc[2], rc[1] + rc[3]);
                    field = nullptr;
                }
        CRect r = field ? CRect(h.x, h.y, h.x + field->w, h.y + field->h) : search_rect_;
        auto *te = new CTextEdit(r, this, 0, plugin_->search().c_str());
        te->setBackColor(CColor(224, 232, 240, 255)); // close to the LCD field, system font like the name edit
        te->setFrameColor(CColor(224, 232, 240, 255));
        te->setFontColor(CColor(20, 40, 120, 255));
        te->setFont(kNormalFontSmall);
        te->setHoriAlign(kLeftText);
        te->setImmediateTextChange(true);
        te->registerTextEditListener(this);
        f->addView(te);
        te->takeFocus();
    }
    void edit_name(const Hit &h)
    {
        auto *f = getFrame();
        if (!f)
            return;
        edit_part_ = h.part;
        editing_search_ = false;
        CRect r(h.x, h.y, h.x + h.element->w, h.y + h.element->h);
        auto *te = new CTextEdit(r, this, 0, plugin_->patch_name(h.part).c_str());
        te->setBackColor(CColor(255, 255, 255, 255)); // the original: white field, system font
        te->setFontColor(CColor(0, 0, 0, 255));
        te->setFrameColor(CColor(255, 255, 255, 255));
        f->addView(te);
        te->takeFocus();
    }

    // a parameter's valuelist as a popup at the click position; the choice sets the parameter
    void popup_menu(const Hit &h, const CPoint &where)
    {
        const std::string param = h.element->get("Parameter");
        const Parameter *p = plugin_->conditions().find(param);
        if (!p)
            return;
        auto *menu = new COptionMenu();
        std::vector<int> values;
        int current = plugin_->ui_value(param, h.part), index = -1;
        for (auto &[v, t] : renderer_.menu_entries(param)) { // the list's order (mapped lists: index, not value)
            if (v == current)
                index = (int)values.size();
            menu->addEntry(t.c_str());
            values.push_back(v);
        }
        if (index >= 0)
            menu->setCurrent(index);
        int part = h.part;
        CPoint loc(where);
        menu->popup(getFrame(), loc, [this, param, part, values](COptionMenu *m) {
            int r = m->getLastResult();
            if (r >= 0 && r < (int)values.size()) {
                plugin_->ui_set(param, part, values[r]);
                invalid();
            }
        });
        menu->forget();
    }

    VirusPlugin *plugin_;
    EditorRenderer renderer_;
    std::map<std::string, SharedPointer<CBitmap>> bitmaps_;
    VstguiCanvas canvas_;
    SharedPointer<CVSTGUITimer> timer_;
    Hit drag_;
    bool partial_ = false;
    std::vector<std::array<int, 4>> drag_rects_;
    double drag_y_ = 0, drag_x_ = 0;
    bool dnd_ = false;             // a patch is being dragged
    int dnd_bank_ = 0, dnd_prog_ = -1;
    int drag_value_ = 0;
};

VirusEditor::VirusEditor(VirusPlugin *plugin) : VSTGUIEditor(plugin), plugin_(plugin)
{
    rect = Steinberg::ViewRect(0, 0, plugin->layout().width, plugin->layout().height);
}

bool PLUGIN_API VirusEditor::open(void *parent, const PlatformType &platformType)
{
    if (frame)
        return false;
    CRect size(0, 0, plugin_->layout().width, plugin_->layout().height);
    frame = new CFrame(size, this);
#if SMTG_OS_LINUX
    // VSTGUI on X11 needs the host's run loop (IRunLoop from the plug frame or the host context)
    if (plugFrame)
        Steinberg::Linux::setupVSTGUIRunloop(plugFrame); // hosts offer IRunLoop on the frame or the host context
    X11::FrameConfig cfg;
    if (auto lf = getPlatformFactory().asLinuxFactory())
        cfg.runLoop = lf->getRunLoop();
    frame->open(parent, platformType, cfg.runLoop ? &cfg : nullptr);
    // X11 has no native menus: VSTGUI's generic menu fades in and uses a large dark theme. Like the original's
    // (Windows) menus: no animation, compact rows, light theme.
    if (auto *pf = frame->getPlatformFrame()) {
        GenericOptionMenuTheme theme;
        theme.menuAnimationTime = 2; // fade-in = time / 2; 0 never completes (transparent menu)
        theme.font = makeOwned<CFontDesc>(kSystemFont->getName(), 11);
        theme.backgroundColor = CColor(250, 250, 250, 252);
        theme.textColor = CColor(0, 0, 0, 255);
        theme.selectedBackgroundColor = CColor(51, 153, 255, 255);
        theme.selectedTextColor = CColor(255, 255, 255, 255);
        theme.disabledTextColor = theme.titleTextColor = CColor(140, 140, 140, 255);
        theme.separatorColor = CColor(200, 200, 200, 255);
        theme.inset = CPoint(3, 2);
        pf->setupGenericOptionMenu(true, &theme);
    }
#else
    frame->open(parent, platformType);
#endif
    frame->addView(new EditorCanvasView(plugin_, size));
    return true;
}

void PLUGIN_API VirusEditor::close()
{
    if (frame) {
        frame->forget();
        frame = nullptr;
    }
}

} // namespace vti
