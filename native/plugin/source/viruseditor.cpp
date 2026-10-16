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
#include "cc/diag.h"
#include "editor/helptext.h"
#include "editor/upscale.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <list>
#include <set>
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

static bool env_debug()
{
    static const bool on = getenv("VTI_NATIVE_DEBUG") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_no_partial() // fallback / test reference: value changes repaint everything (as before 2026-10-14)
{
    static const bool on = getenv("VTI_NATIVE_NO_PARTIAL") != nullptr;
    return on;
}

// the 1x images only, also on scaled displays (reference / fallback for the sharper copies, skin option A)
static bool env_no_hidpi()
{
    static const bool on = getenv("VTI_NATIVE_NO_HIDPI") != nullptr;
    return on;
}

static bool env_drawtime()
{
    static const bool on = getenv("VTI_NATIVE_DRAWTIME") != nullptr; // read once (called on hot paths)
    return on;
}


// Canvas on a VSTGUI draw context; bitmaps are created once from the user's PNGs.
#if SMTG_OS_LINUX
static cairo_surface_t *ref = nullptr; // a 1x1 surface on a frame's window: create_similar makes pixmaps
static int open_frames = 0;            // editors with an X11 frame in this process

// VSTGUI closes its X connection when the last frame goes (X11::RunLoop::exit) without cairo_device_finish: cairo
// keeps that connection's data under its address, and the next editor's new connection often gets the same address
// - an assertion in cairo-xcb-screen.c (_get_screen_index) aborted the host on reopening the editor (REAPER,
// 2026-10-16; since 0.8.0). `ref` also belonged to the old connection. Called before the last frame is destroyed.
static void x11_last_frame_closing()
{
    if (ref) {
        cairo_surface_destroy(ref);
        ref = nullptr;
    }
    auto *conn = X11::RunLoop::instance().getXcbConnection();
    if (!conn)
        return;
    // the device belongs to the connection; a 1x1 surface on the root window gives it without a frame window
    xcb_screen_t *sc = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    xcb_visualtype_t *vt = nullptr;
    for (auto d = xcb_screen_allowed_depths_iterator(sc); d.rem && !vt; xcb_depth_next(&d))
        for (auto v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
            if (v.data->visual_id == sc->root_visual) {
                vt = v.data;
                break;
            }
    if (!vt)
        return;
    cairo_surface_t *s = cairo_xcb_surface_create(conn, sc->root, vt, 1, 1);
    cairo_device_t *dev = cairo_device_reference(cairo_surface_get_device(s));
    cairo_surface_destroy(s);
    if (dev) {
        cairo_device_finish(dev); // flushes and forgets the connection; its surfaces left over are freed later
        cairo_device_destroy(dev);
    }
}

// an X pixmap copy of `bmp` for drawing into the frame's window (server-side composite, GPU with glamor)
static SharedPointer<CBitmap> server_bitmap(CFrame *frame, CBitmap *bmp)
{
    if (!frame || !bmp || getenv("VTI_NATIVE_NO_PIXMAPS"))
        return nullptr;
    auto *cb = dynamic_cast<Cairo::Bitmap *>(bmp->getPlatformBitmap().get());
    if (!cb || !cb->surface || cairo_surface_get_type(cb->surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return nullptr;
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

// Images taller than this are split into tiles: Direct2D cannot create a bitmap over 16384 px on a side, and the
// big knob filmstrips are 16512 px tall (they stayed invisible on Windows). VTI_NATIVE_TILE overrides it (tests).
static int tile_limit()
{
    static int v = [] {
        const char *e = getenv("VTI_NATIVE_TILE");
        return e && atoi(e) > 0 ? atoi(e) : 16384;
    }();
    return v;
}

// rows [y0, y0 + h) of `bmp` as a new bitmap (same platform pixel format)
static SharedPointer<CBitmap> crop_rows(CBitmap *bmp, int y0, int h)
{
    auto src = bmp->getPlatformBitmap();
    int w = (int)bmp->getWidth();
    auto pb = getPlatformFactory().createBitmap(CPoint(w, h));
    if (!src || !pb)
        return nullptr;
    auto in = src->lockPixels(false);
    auto out = pb->lockPixels(false);
    if (!in || !out)
        return nullptr;
    for (int y = 0; y < h; y++)
        memcpy(out->getAddress() + (size_t)y * out->getBytesPerRow(),
               in->getAddress() + (size_t)(y0 + y) * in->getBytesPerRow(), (size_t)w * 4);
    return makeOwned<CBitmap>(pb);
}

// the pixels of `pb` (premultiplied) in the platform's channel order `fmt`
static vti::Image read_pixels(IPlatformBitmap *pb, IPlatformBitmapPixelAccess::PixelFormat &fmt)
{
    vti::Image im;
    auto in = pb ? pb->lockPixels(true) : nullptr;
    if (!in)
        return im;
    fmt = in->getPixelFormat();
    im.w = (int)pb->getSize().x, im.h = (int)pb->getSize().y;
    im.px.resize((size_t)im.w * im.h * 4);
    for (int y = 0; y < im.h; y++)
        memcpy(&im.px[(size_t)y * im.w * 4], in->getAddress() + (size_t)y * in->getBytesPerRow(), (size_t)im.w * 4);
    return im;
}

// rows [y0, y1) of `im` as a platform bitmap drawn at `scale` (its logical size = pixels / scale); nullptr if the
// platform stores another channel order than `fmt`
static PlatformBitmapPtr make_bitmap(const vti::Image &im, int y0, int y1, double scale,
                                     IPlatformBitmapPixelAccess::PixelFormat fmt)
{
    auto pb = getPlatformFactory().createBitmap(CPoint(im.w, y1 - y0));
    if (!pb)
        return nullptr;
    {
        auto out = pb->lockPixels(true);
        if (!out || out->getPixelFormat() != fmt)
            return nullptr;
        for (int y = y0; y < y1; y++)
            memcpy(out->getAddress() + (size_t)(y - y0) * out->getBytesPerRow(), &im.px[(size_t)y * im.w * 4],
                   (size_t)im.w * 4);
    } // unlocked: the pixels are written back (Direct2D)
    pb->setScaleFactor(scale);
    return pb;
}

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
            const int lim = tile_limit();
            if (bmp && bmp->getHeight() > lim) {
                std::vector<SharedPointer<CBitmap>> t;
                for (int y = 0; y < (int)bmp->getHeight(); y += lim)
                    t.push_back(crop_rows(bmp, y, std::min(lim, (int)bmp->getHeight() - y)));
                bool ok = true;
                for (auto &b : t)
                    ok = ok && b;
                if (env_debug())
                    fprintf(stderr, "editor: %s %dx%d split into %zu tiles%s\n", name.c_str(), (int)bmp->getWidth(),
                            (int)bmp->getHeight(), t.size(), ok ? "" : " - FAILED, drawn whole");
                if (ok)
                    tiles_[bmp.get()] = {lim, std::move(t)};
            }
            it = cache_.emplace(name, bmp).first;
        }
#if SMTG_OS_LINUX
        if (frame && it->second && !tiles_.count(it->second.get())) {
            auto sv = server_.find(name);
            if (sv == server_.end()) {
                sv = server_.emplace(name, server_bitmap(frame, it->second.get())).first;
                if (sv->second)
                    base_[sv->second.get()] = it->second.get();
            }
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
        if (scale > 1.01 && !sharp_.count((CBitmap *)img))
            sharpen((CBitmap *)img);
        auto t = tiles_.find((CBitmap *)img);
        if (t == tiles_.end()) {
            ((CBitmap *)img)->draw(ctx, CRect(dx, dy, dx + w, dy + h), CPoint(sx, sy), (float)alpha);
            return;
        }
        const int lim = t->second.h; // a source rectangle can straddle two tiles
        while (h > 0) {
            int i = sy / lim, off = sy - i * lim, n = std::min(h, lim - off);
            if (i >= (int)t->second.t.size())
                break;
            t->second.t[i]->draw(ctx, CRect(dx, dy, dx + w, dy + n), CPoint(sx, off), (float)alpha);
            sy += n, dy += n, h -= n;
        }
    }
    void frames(void *img, int frame_h) override
    {
        if (img && frame_h > 0)
            frame_h_.emplace((CBitmap *)img, frame_h);
    }

    // device pixels per editor pixel (display scaling x editor size). Above 1 the images are drawn from copies
    // enlarged to it (vti::upscale), made when first drawn; a new scale drops every bitmap made so far.
    double scale = 1;
    void set_scale(double s)
    {
        if (s == scale)
            return;
        scale = s;
        cache_.clear(), tiles_.clear(), server_.clear(), base_.clear(), sharp_.clear(), frame_h_.clear();
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
    void push_clip(int x, int y, int w, int h) override
    {
        CRect old;
        ctx->getClipRect(old);
        clips_.push_back(old);
        CRect r(x, y, x + w, y + h);
        r.bound(old);
        ctx->setClipRect(r);
    }
    void pop_clip() override
    {
        if (!clips_.empty()) {
            ctx->setClipRect(clips_.back());
            clips_.pop_back();
        }
    }

private:
    const AccessFiles &files_;
    std::map<std::string, SharedPointer<CBitmap>> &cache_;
    struct Tiles {
        int h; // logical rows per tile
        std::vector<SharedPointer<CBitmap>> t;
    };
    std::map<CBitmap *, Tiles> tiles_;     // of the images in cache_ over tile_limit()
    std::map<CBitmap *, CBitmap *> base_;  // server copy -> the image in cache_ it was made from
    std::set<CBitmap *> sharp_;            // drawn bitmaps that have their copy at `scale` (or cannot have one)
    std::map<CBitmap *, int> frame_h_;     // frames() hints
    std::vector<CRect> clips_;

    // adds the copy at `scale` to `drawn`: to the image itself (offscreen drawing), to its server copy (Linux
    // window), or as new tiles (images over the Direct2D limit, which an enlarged copy exceeds even sooner)
    void sharpen(CBitmap *drawn)
    {
        sharp_.insert(drawn);
        if (env_no_hidpi())
            return;
        auto b = base_.find(drawn);
        CBitmap *src = b != base_.end() ? b->second : drawn;
        auto fh = frame_h_.find(drawn);
        const int f = fh != frame_h_.end() ? fh->second : 0;
        auto t0 = std::chrono::steady_clock::now();
        auto t = tiles_.find(src);
        PlatformBitmapPtr have = src->getBestPlatformBitmapForScaleFactor(scale);
        if (!have || have->getScaleFactor() != scale)
            have = nullptr; // the image's own copy, made when the offscreen drew it first
        vti::Image im, hi;
        auto fmt = IPlatformBitmapPixelAccess::kBGRA;
        if (t != tiles_.end() || !have) {
            im = read_pixels(src->getPlatformBitmap().get(), fmt);
            if (im.w <= 0)
                return;
            const int alpha = fmt == IPlatformBitmapPixelAccess::kARGB || fmt == IPlatformBitmapPixelAccess::kABGR ? 0 : 3;
            hi = vti::upscale(im, scale, f, alpha);
        }
        if (t != tiles_.end()) {
            // tiles of at most tile_limit() device pixels, cut at frame boundaries where the frames allow it
            int th = std::max(1, (int)(tile_limit() / scale));
            if (f > 0 && th >= f)
                th = th / f * f;
            std::vector<SharedPointer<CBitmap>> nt;
            for (int y = 0; y < im.h; y += th) {
                const int n = std::min(th, im.h - y);
                auto tile = crop_rows(src, y, n);
                int y0 = std::min(hi.h, (int)std::lround(y * scale)), y1 = std::min(hi.h, (int)std::lround((y + n) * scale));
                auto pb = y1 > y0 ? make_bitmap(hi, y0, y1, scale, fmt) : nullptr;
                if (!tile || !pb)
                    return; // keep the 1x tiles
                tile->addBitmap(pb);
                nt.push_back(tile);
            }
            t->second = {th, std::move(nt)};
        } else {
            if (!have) {
                have = make_bitmap(hi, 0, hi.h, scale, fmt);
                if (!have)
                    return;
                src->addBitmap(have);
            }
#if SMTG_OS_LINUX
            if (drawn != src && frame) { // the server copy gets its own enlarged pixmap
                auto tmp = makeOwned<CBitmap>(have);
                if (auto sv = server_bitmap(frame, tmp))
                    drawn->addBitmap(sv->getPlatformBitmap());
            }
#endif
        }
        if (env_drawtime() && im.w > 0)
            fprintf(stderr, "editor: %dx%d image enlarged x%.2f in %.1f ms\n", im.w, im.h, scale,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
};

class EditorCanvasView : public CView, public IControlListener, public TextEditListenerAdapter, public IKeyboardHook,
                        public IMouseObserver {
public:
    EditorCanvasView(VirusPlugin *plugin, const CRect &r, VirusEditor *editor)
        : CView(r), plugin_(plugin), editor_(editor), renderer_(plugin->layout(), plugin->conditions(), plugin->device(), plugin->editor_device()),
          canvas_(plugin->files(), bitmaps_)
    {
        renderer_.values = [this](const std::string &path, int part) { return plugin_->ui_value(path, part); };
        renderer_.patch_name = [this](int part) { return plugin_->patch_name(part); };
        renderer_.bank_patch = [this](int bank, int prog) { return plugin_->bank_patch(bank, prog); };
        renderer_.live_text = [this](const std::string &p) { return plugin_->live_text(p); };
        renderer_.proxy_target = [this](const std::string &p, int part) { return plugin_->softknob_target(p, part); };
        renderer_.meter = [this](int ch) { return plugin_->meter(ch); };
        renderer_.osc_waves = plugin_->osc_waves();
        // device changes (knobs on the Virus, automation) repaint the view
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) {
            plugin_->diag_report();
            if (plugin_->ui_changed() && !partial_) { // during a drag the drag repaints its own area
                plugin_->diag.dirty_ticks++;
                plugin_->lcd_changed();
                plugin_->values_changed();
                plugin_->take_changed();
                invalid();
            } else if (!partial_ && plugin_->values_changed()) {
                // automation / Virus panel: repaint the elements that read the changed values in the last full
                // repaint (deps_), and everything when a value decides what is shown (conditions, the panel part)
                bool overflow = false, full = deps_.rects.empty() || env_no_partial();
                auto changed = plugin_->take_changed(&overflow);
                for (auto &p : changed)
                    if (deps_.cond.count(p) || p == plugin_->layout().frame_parameter)
                        full = true;
                if (full || overflow) {
                    plugin_->diag.dirty_ticks++;
                    invalid();
                } else {
                    for (auto &p : changed) {
                        auto it = deps_.rects.find(p);
                        if (it != deps_.rects.end())
                            for (auto &rc : it->second)
                                invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
                    }
                    if (plugin_->lcd_changed())
                        invalidRect(CRect(200, 470, 560, 512));
                }
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
            } else
                preload_step();
        }, 50);
    }

    // idle ticks: decode the skin's bitmaps ahead of use (Linux: and upload them as pixmaps), so the first visit of a
    // page does not load its bitmaps while drawing
    void preload_step()
    {
        if (preload_pos_ == 0 && preload_.empty()) {
            std::set<std::string> names;
            std::function<void(const Element &)> walk = [&](const Element &e) {
                for (auto &kv : e.attr)
                    if (kv.second.size() > 4) {
                        std::string ext = kv.second.substr(kv.second.size() - 4);
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)tolower(ch); });
                        if (ext == ".png")
                            names.insert(kv.second);
                    }
                for (auto &c : e.children)
                    walk(c);
            };
            for (auto &e : plugin_->layout().elements)
                walk(e);
            preload_.assign(names.begin(), names.end());
            if (preload_.empty())
                preload_pos_ = 1; // nothing to do
        }
        if (!getFrame() || preload_pos_ >= preload_.size())
            return;
        canvas_.frame = getFrame();
        for (int i = 0; i < 6 && preload_pos_ < preload_.size(); i++)
            canvas_.image(preload_[preload_pos_++]);
    }

    // the areas that show `param`: its own controls, plus every element that read it in the last full repaint (the
    // envelope display is bound to Attack but also draws Decay, Sustain, Slope and Release)
    std::vector<std::array<int, 4>> shown_rects(const std::string &param)
    {
        auto v = renderer_.param_rects(param);
        auto it = deps_.rects.find(param);
        if (it != deps_.rects.end())
            v.insert(v.end(), it->second.begin(), it->second.end());
        return v;
    }

    void draw(CDrawContext *ctx) override { drawRect(ctx, getViewSize()); }

    // Esc leaves the help mode (a frame hook: the view has no keyboard focus)
    bool attached(CView *parent) override
    {
        bool ok = CView::attached(parent);
        if (auto *f = getFrame()) {
            f->registerKeyboardHook(this);
            f->registerMouseObserver(this); // hover in help mode: views get moves only while a button is down
        }
        return ok;
    }
    bool removed(CView *parent) override
    {
        if (auto *f = getFrame()) {
            f->unregisterKeyboardHook(this);
            f->unregisterMouseObserver(this);
        }
        return CView::removed(parent);
    }
    // IMouseObserver: every mouse event of the frame (frame coordinates, before its zoom transform)
    void onMouseEntered(CView *, CFrame *) override {}
    void onMouseExited(CView *, CFrame *) override {}
    void onMouseEvent(MouseEvent &event, CFrame *frame) override
    {
        if (!help_ || event.type != EventType::MouseMove || event.buttonState.isLeft())
            return;
        (void)frame;
        CPoint p = static_cast<MousePositionEvent &>(event).mousePosition; // already through the frame's zoom
        p.offset(-getViewSize().left, -getViewSize().top);
        if (env_debug())
            fprintf(stderr, "help hover %.0f,%.0f\n", p.x, p.y);
        help_hover(p);
    }
    void onKeyboardEvent(KeyboardEvent &event, CFrame *) override
    {
        if (help_ && event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) {
            set_help(false);
            event.consumed = true;
        }
    }

    void drawRect(CDrawContext *ctx, const CRect &update) override
    {
        renderer_.part = plugin_->ui_value(plugin_->layout().frame_parameter, -1);
        canvas_.ctx = ctx;
        CRect r(update);
        r.offset(-getViewSize().left, -getViewSize().top);
        renderer_.clip_x0 = (int)std::floor(r.left), renderer_.clip_y0 = (int)std::floor(r.top);
        renderer_.clip_x1 = (int)std::ceil(r.right), renderer_.clip_y1 = (int)std::ceil(r.bottom);
        auto t0 = std::chrono::steady_clock::now();
        // device pixels per editor pixel: VSTGUI picks the bitmap copy for it (CDrawContext::drawBitmap)
        const CGraphicsTransform &tm = ctx->getCurrentTransform();
        double scale = ctx->getScaleFactor() * (tm.m11 == tm.m22 && tm.m12 == 0 && tm.m21 == 0 ? tm.m11 : 1.0);
        if (env_no_hidpi() || scale < 1.01)
            scale = 1;
        if (scale != canvas_.scale) {
            canvas_.set_scale(scale);
            statics_.clear();
        }
        // the static layer (backgrounds, panels, labels) lives in an offscreen bitmap, rebuilt only when the
        // visible structure changes (page, oscillator mode, ...); every repaint copies it and draws the controls
        // the structure can only change with full repaints (page switch, menus, state); partial ones (knob drags,
        // meters, LCD) reuse the cache without walking the conditions
        bool full_update = r.getWidth() >= getViewSize().getWidth() - 1 && r.getHeight() >= getViewSize().getHeight() - 1;
        std::vector<const Element *> sig;
        if (statics_.empty() || full_update)
            sig = renderer_.static_signature();
        bool rebuild = false;
        if (statics_.empty() || (full_update && sig != statics_.front().key)) {
            // the last pages' static layers stay cached: switching back is a copy instead of a rebuild
            auto hit = std::find_if(statics_.begin(), statics_.end(), [&](const StaticLayer &l) { return l.key == sig; });
            if (hit != statics_.end())
                statics_.splice(statics_.begin(), statics_, hit);
            else
                rebuild = true;
        }
        if (rebuild) {
            CRect full = getViewSize();
            // at device resolution: the offscreen has scale x the pixels and draws through a scale transform (the
            // Cairo bitmap context ignores a bitmap's scale factor, Core Graphics applies it - so set it afterwards)
            const CPoint px(std::ceil(full.getWidth() * scale), std::ceil(full.getHeight() * scale));
            if (auto off = COffscreenContext::create(px)) {
                off->beginDraw();
                canvas_.ctx = off;
                canvas_.frame = nullptr; // CPU images into the CPU offscreen
                int c0 = renderer_.clip_x0, c1 = renderer_.clip_y0, c2 = renderer_.clip_x1, c3 = renderer_.clip_y1;
                renderer_.clip_x0 = renderer_.clip_y0 = -100000;
                renderer_.clip_x1 = renderer_.clip_y1 = 100000;
                renderer_.layer = 1;
                {
                    CDrawContext::Transform tr(*off, CGraphicsTransform().scale(scale, scale));
                    renderer_.render(canvas_);
                }
                renderer_.clip_x0 = c0, renderer_.clip_y0 = c1, renderer_.clip_x1 = c2, renderer_.clip_y1 = c3;
                off->endDraw();
                SharedPointer<CBitmap> bg = off->getBitmap();
                if (scale != 1)
                    bg->getPlatformBitmap()->setScaleFactor(scale);
#if SMTG_OS_LINUX
                if (auto sb = server_bitmap(getFrame(), bg))
                    bg = sb; // the cache as an X pixmap too
#endif
                statics_.push_front({sig, bg});
                while (statics_.size() > (scale > 1.5 ? 3u : 8u)) // ~1.9 MB each at 1x, scale^2 x that
                    statics_.pop_back();
                canvas_.ctx = ctx;
                if (env_drawtime())
                    fprintf(stderr, "static layer rebuilt\n");
            }
        }
        canvas_.frame = getFrame(); // into the window: server-side bitmaps (Linux)
        if (!statics_.empty() && full_update) { // small areas: drawing their few elements directly is cheaper on X11
            CRect dst(update);
            statics_.front().bg->draw(ctx, dst, CPoint(dst.left - getViewSize().left, dst.top - getViewSize().top));
            renderer_.layer = 2;
        } else
            renderer_.layer = 0;
        if (full_update) { // record what the controls read, for partial repaints of value changes
            deps_.cond.clear();
            deps_.rects.clear();
            renderer_.deps = &deps_;
        }
        renderer_.render(canvas_);
        renderer_.deps = nullptr;
        if (plugin_->sync_paused()) // under the sync page's progress bar (the original hides the LCD meanwhile)
            renderer_.status_text(canvas_, "FNT_LCD_5x7_Rot.png", "SYNC PAUSED WHILE YOU PLAY", 480, 290, 381, 12);
        else if (int eta = plugin_->sync_eta(); eta >= 60) { // a first sync: tell how long, not "a couple of seconds"
            char t[64];
            snprintf(t, sizeof t, "READING THE VIRUS' BANKS: ABOUT %d MIN LEFT", (eta + 59) / 60);
            renderer_.status_text(canvas_, "FNT_LCD_5x7_Rot.png", t, 480, 290, 381, 12);
        }
        if (help_)
            draw_help(ctx);
        {
            long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            plugin_->diag.draw_us += us;
            (r.getWidth() >= getViewSize().getWidth() - 1 ? plugin_->diag.full_draws : plugin_->diag.part_draws)++;
        }
        if (env_drawtime())
            fprintf(stderr, "editor draw %dx%d: %.2f ms\n", (int)r.getWidth(), (int)r.getHeight(),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (env_debug()) {
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
        if (env_debug())
            fprintf(stderr, "mouse down %.0f,%.0f -> %s %s\n", event.mousePosition.x, event.mousePosition.y,
                    h.element ? h.element->type.c_str() : "-", h.element ? h.element->get("Parameter").c_str() : "");
        // right-click where there is no control: the editor's size (the original has no such setting)
        if (event.buttonState.isRight() && (!h.element || h.element->get("Parameter").empty())) {
            size_menu(event.mousePosition);
            event.consumed = true;
            return;
        }
        // "?" (the original's help menu of manuals): our help mode; in help mode a click pins a control's note and
        // changes nothing - only the page/part/envelope selectors still work, so every page can be read
        if (h.element && h.element->get("Parameter") == "VirusControl/Help Menu") {
            set_help(!help_);
            event.consumed = true;
            return;
        }
        if (help_ && !(h.element && help_navigates(h.element->get("Parameter")))) {
            h = renderer_.hit_test((int)event.mousePosition.x, (int)event.mousePosition.y, true); // disabled too
            HelpText t;
            help_pinned_ = h.element && help_for(h.element->get("Parameter"), t);
            help_hit_ = help_pinned_ ? h : Hit{};
            invalid();
            event.consumed = true;
            return;
        }
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
            std::string t = plugin_->softknob_target(param, h.part); // a soft knob: its target's default
            plugin_->ui_set(param, h.part, plugin_->conditions().default_value(t.empty() ? param : t));
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
        if (help_ && !event.buttonState.isLeft()) {
            help_hover(event.mousePosition);
            return;
        }
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
        const std::string target = plugin_->softknob_target(param, drag_.part); // a soft knob: its target's range
        const Parameter *p = plugin_->conditions().find(target.empty() ? param : target);
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
            drag_rects_ = shown_rects(param);
        for (auto &rc : drag_rects_) // every control of this parameter (knob + value display ...)
            invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
        for (auto &other : plugin_->take_changed()) // linked parameters (LFO/filter links) move along
            if (other != param)
                for (auto &rc : shown_rects(other))
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
    // help mode ("?"): frames around the explained controls, a note for the one under the mouse (or clicked)
    bool help_ = false, help_pinned_ = false;
    Hit help_hit_;
    void set_help(bool on)
    {
        help_ = on;
        help_pinned_ = false;
        help_hit_ = {};
        invalid();
    }
    // the note follows the mouse; a pinned (clicked) one stays while the mouse is over no explained control
    void help_hover(const CPoint &p)
    {
        Hit h = renderer_.hit_test((int)p.x, (int)p.y, true); // disabled controls too (with the reason)
        HelpText t;
        if (!(h.element && help_for(h.element->get("Parameter"), t)))
            h = help_pinned_ ? help_hit_ : Hit{};
        else
            help_pinned_ = false;
        if (h.element != help_hit_.element || h.part != help_hit_.part) {
            help_hit_ = h;
            invalid();
        }
    }
    static bool help_navigates(const std::string &param)
    {
        return param == "VirusControl/SelectedPage" || param == "VirusControl/SelectedUtilityPage" ||
               param == "VirusControl/Selected Section" || param == "Multi Parameters/Panel Part" ||
               param == "Envelope/Select";
    }
    static std::vector<std::string> wrap(CDrawContext *ctx, const std::string &text, double width)
    {
        std::vector<std::string> lines;
        std::string line, word;
        auto flush_word = [&]() {
            if (word.empty())
                return;
            std::string tryline = line.empty() ? word : line + " " + word;
            if (!line.empty() && ctx->getStringWidth(tryline.c_str()) > width) {
                lines.push_back(line);
                line = word;
            } else
                line = tryline;
            word.clear();
        };
        for (char ch : text) {
            if (ch == ' ')
                flush_word();
            else
                word += ch;
        }
        flush_word();
        if (!line.empty())
            lines.push_back(line);
        return lines;
    }
    void draw_help(CDrawContext *ctx)
    {
        const CColor frame_col(120, 190, 255, 150), hot_col(120, 190, 255, 255);
        ctx->setLineWidth(1);
        ctx->setDrawMode(kAntiAliasing);
        HelpText t;
        for (auto &c : renderer_.visible_controls(true))
            if (help_for(c.element->get("Parameter"), t)) {
                ctx->setFrameColor(frame_col);
                ctx->drawRect(CRect(c.x + 0.5, c.y + 0.5, c.x + c.element->w - 0.5, c.y + c.element->h - 0.5),
                              kDrawStroked);
            }
        const CRect view(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        // the hint, above the "?" button
        auto q = renderer_.param_rects("VirusControl/Help Menu");
        SharedPointer<CFontDesc> font = makeOwned<CFontDesc>(*kNormalFontSmall);
        SharedPointer<CFontDesc> bold = makeOwned<CFontDesc>(*kNormalFontSmall);
        bold->setStyle(kBoldFace);
        ctx->setFont(font);
        const char *hint = "Help: point at a control, click to keep the note. Esc or ? to leave.";
        double hw = ctx->getStringWidth(hint) + 12;
        double hx = q.empty() ? view.right - hw - 4 : std::min<double>(view.right - hw - 4, q[0][0] + q[0][2] - hw);
        double hy = q.empty() ? view.bottom - 40 : q[0][1] - 20;
        CRect hint_r(hx, hy, hx + hw, hy + 16);
        ctx->setFillColor(CColor(20, 22, 28, 230));
        ctx->drawRect(hint_r, kDrawFilled);
        ctx->setFontColor(CColor(220, 230, 240, 255));
        ctx->drawString(hint, hint_r, kCenterText);
        if (!q.empty()) {
            ctx->setFrameColor(hot_col);
            ctx->setLineWidth(2);
            ctx->drawRect(CRect(q[0][0], q[0][1], q[0][0] + q[0][2], q[0][1] + q[0][3]), kDrawStroked);
        }
        if (!help_hit_.element || !help_for(help_hit_.element->get("Parameter"), t))
            return;
        const Element &e = *help_hit_.element;
        CRect cr(help_hit_.x, help_hit_.y, help_hit_.x + e.w, help_hit_.y + e.h);
        ctx->setFrameColor(hot_col);
        ctx->setLineWidth(2);
        ctx->drawRect(cr, kDrawStroked);
        // the note: title + wrapped text, below the control if it fits, else above; kept inside the editor
        const double w = 260, pad = 8, lh = 14;
        ctx->setFont(font);
        auto lines = wrap(ctx, t.text, w - 2 * pad);
        if (help_hit_.inactive) { // why it is greyed out now: the condition of the page that shows it, or the
                                  // parameters its own condition tests
            std::string why = help_hit_.disabled ? inactive_reason(help_hit_.why) : depends_reason(help_hit_.depends);
            for (auto &l : wrap(ctx, why.empty() ? "Inactive with the current settings." : "Inactive now: " + why + ".",
                                w - 2 * pad))
                lines.push_back(l);
        }
        double h = pad * 2 + lh * (1 + lines.size()) + 2;
        double x = std::max(4.0, std::min(view.right - w - 4, cr.left));
        double y = cr.bottom + 6 + h <= view.bottom - 4 ? cr.bottom + 6 : std::max(4.0, cr.top - 6 - h);
        CRect box(x, y, x + w, y + h);
        if (auto path = owned(ctx->createRoundRectGraphicsPath(box, 6))) {
            ctx->setFillColor(CColor(24, 26, 32, 240));
            ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
            ctx->setFrameColor(hot_col);
            ctx->setLineWidth(1);
            ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
        ctx->setFont(bold);
        ctx->setFontColor(CColor(255, 255, 255, 255));
        ctx->drawString(t.title.c_str(), CRect(x + pad, y + pad, x + w - pad, y + pad + lh), kLeftText);
        ctx->setFont(font);
        ctx->setFontColor(CColor(215, 222, 232, 255));
        for (size_t i = 0; i < lines.size(); i++) {
            double ly = y + pad + lh * (i + 1) + 2;
            ctx->drawString(lines[i].c_str(), CRect(x + pad, ly, x + w - pad, ly + lh), kLeftText);
        }
    }
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
    EditorRenderer::Deps deps_;      // what the controls read (filled by full repaints)
    struct StaticLayer {
        std::vector<const Element *> key; // the visible conditional static elements (page, modes, ...)
        SharedPointer<CBitmap> bg;        // the static layer drawn with them
    };
    std::list<StaticLayer> statics_;   // front = current; the last 8 pages
    std::vector<std::string> preload_; // the skin's bitmaps, loaded a few per idle tick (first page visits)
    size_t preload_pos_ = 0;
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
        menu->popup(getFrame(), menu_at(where), [this, part, save, banks](COptionMenu *m) {
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
        menu->popup(getFrame(), menu_at(where), [this, param, dev, entries](COptionMenu *m) {
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
        menu->popup(getFrame(), menu_at(where), [this](COptionMenu *m) {
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

    // VSTGUI's generic menu (X11) converts the position with translateToGlobal, which includes the frame's zoom, and
    // then places the menu among the frame's unzoomed views: at 200 % every menu opened at twice the click position.
    // The native menus of Windows and macOS take the position as it is.
    CPoint menu_at(const CPoint &where) const
    {
#if SMTG_OS_LINUX
        if (auto *f = getFrame())
            if (double z = f->getZoom(); z > 0 && z != 1)
                return CPoint(where.x / z, where.y / z);
#endif
        return where;
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
            // "-G": the original's marker for an entry shown greyed (e.g. "-GUSB3 L+R" while the third USB output is
            // off, E158) - disabled here, without the marker
            const bool grey = t.rfind("-G", 0) == 0;
            menu->addEntry(grey ? t.substr(2).c_str() : t.c_str(), -1, grey ? CMenuItem::kDisabled : CMenuItem::kNoFlags);
            values.push_back(v);
        }
        if (index >= 0)
            menu->setCurrent(index);
        int part = h.part;
        CPoint loc(where);
        menu->popup(getFrame(), menu_at(loc), [this, param, part, values](COptionMenu *m) {
            int r = m->getLastResult();
            if (r >= 0 && r < (int)values.size()) {
                plugin_->ui_set(param, part, values[r]);
                invalid();
            }
        });
        menu->forget();
    }

    VirusPlugin *plugin_;
    VirusEditor *editor_;

    void size_menu(const CPoint &where)
    {
        static const double sizes[] = {1.0, 1.25, 1.5, 1.75, 2.0};
        auto *menu = new COptionMenu();
        for (double z : sizes) {
            char t[32];
            snprintf(t, sizeof t, "Size %d %%", (int)std::lround(z * 100));
            auto *item = menu->addEntry(t);
            if (std::fabs(editor_->user_zoom() - z) < 0.01)
                item->setChecked(true);
        }
        menu->popup(getFrame(), menu_at(where), [this](COptionMenu *m) {
            int idx = m->getLastResult();
            if (idx >= 0 && idx < (int)(sizeof sizes / sizeof sizes[0]))
                editor_->set_user_zoom(sizes[idx]);
        });
        menu->forget();
    }
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

// the user's size choice, kept for every editor (not per project): <plug-in folder>/ui-zoom ("1.5");
// VTI_NATIVE_ZOOM overrides it (tests)
static std::string zoom_file() { return vti::cc::plugin_dir() + "/ui-zoom"; }

static double load_zoom()
{
    double z = 1.0;
    if (const char *e = getenv("VTI_NATIVE_ZOOM"))
        z = atof(e);
    else if (FILE *f = fopen(zoom_file().c_str(), "r")) {
        if (fscanf(f, "%lf", &z) != 1)
            z = 1.0;
        fclose(f);
    }
    return z >= 0.5 && z <= 4.0 ? z : 1.0;
}

VirusEditor::VirusEditor(VirusPlugin *plugin) : VSTGUIEditor(plugin), plugin_(plugin), user_zoom_(load_zoom())
{
    apply_zoom(); // no frame yet: only the size the host asks for before open()
}

Steinberg::tresult PLUGIN_API VirusEditor::queryInterface(const Steinberg::TUID iid, void **obj)
{
    QUERY_INTERFACE(iid, obj, Steinberg::IPlugViewContentScaleSupport::iid, Steinberg::IPlugViewContentScaleSupport)
    return VSTGUIEditor::queryInterface(iid, obj);
}

void VirusEditor::apply_zoom()
{
    if (!frame) {
        rect = Steinberg::ViewRect(0, 0, (int)std::lround(plugin_->layout().width * zoom()),
                                   (int)std::lround(plugin_->layout().height * zoom()));
        return;
    }
    // scales drawing and mouse events; the new size goes to the host through beforeSizeChange (resizeView); if the
    // host refuses, VSTGUI keeps the old zoom
    frame->setZoom(zoom());
}

Steinberg::tresult PLUGIN_API VirusEditor::setContentScaleFactor(ScaleFactor factor)
{
    content_scale_ = factor > 0 ? factor : 1.0;
    apply_zoom();
    return Steinberg::kResultTrue;
}

void VirusEditor::set_user_zoom(double z)
{
    if (z == user_zoom_)
        return;
    user_zoom_ = z;
    apply_zoom();
    if (FILE *f = fopen(zoom_file().c_str(), "w")) {
        fprintf(f, "%g\n", z);
        fclose(f);
    }
}

bool VirusEditor::beforeSizeChange(const CRect &newSize, const CRect &)
{
    if (resizing_ || !plugFrame)
        return true;
    resizing_ = true; // the host answers with onSize, which sets the frame size again
    Steinberg::ViewRect vr(0, 0, (int)std::lround(newSize.getWidth()), (int)std::lround(newSize.getHeight()));
    bool ok = plugFrame->resizeView(this, &vr) == Steinberg::kResultTrue;
    resizing_ = false;
    return ok;
}

Steinberg::tresult PLUGIN_API VirusEditor::onSize(Steinberg::ViewRect *newSize)
{
    bool was = resizing_;
    resizing_ = true;
    auto r = VSTGUIEditor::onSize(newSize);
    resizing_ = was;
    return r;
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
    open_frames++;
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
    frame->addView(new EditorCanvasView(plugin_, size, this));
    if (zoom() != 1.0) { // the host already got the scaled size from getSize()
        resizing_ = true;
        frame->setZoom(zoom());
        resizing_ = false;
    }
    return true;
}

void PLUGIN_API VirusEditor::close()
{
    if (frame) {
#if SMTG_OS_LINUX
        if (--open_frames == 0)
            x11_last_frame_closing();
#endif
        frame->forget();
        frame = nullptr;
    }
}

} // namespace vti
