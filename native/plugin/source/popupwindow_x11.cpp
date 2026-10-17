// popupwindow_x11.cpp - popup menu windows on Linux (X11, also under XWayland): an override-redirect window on the
// root, like the menus of X11 toolkits, so the window manager neither decorates, moves nor focuses it. See
// popupwindow.h.
#include "popupwindow.h"
#include "vstgui/lib/platform/linux/x11platform.h"
#include "vstgui/lib/platform/platform_x11.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <xcb/xcb.h>

using namespace VSTGUI;

namespace vti {

namespace {

xcb_connection_t *conn() { return X11::RunLoop::instance().getXcbConnection(); }

xcb_screen_t *screen_of(xcb_connection_t *c)
{
    return xcb_setup_roots_iterator(xcb_get_setup(c)).data;
}

xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    auto *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}

// the X window of a VSTGUI frame
xcb_window_t window_of(CFrame *frame)
{
    auto *pf = frame ? frame->getPlatformFrame() : nullptr;
    auto *x = dynamic_cast<X11::IX11Frame *>(pf);
    return x ? x->getX11WindowID() : 0;
}

class X11PopupWindow : public PopupWindow {
public:
    ~X11PopupWindow() override
    {
        if (frame_)
            frame_->forget(); // destroys VSTGUI's child window
        if (auto *c = conn(); c && win_) {
            xcb_destroy_window(c, win_);
            xcb_flush(c);
        }
    }
    void move(const NativeRect &r) override
    {
        const uint32_t v[] = {(uint32_t)r.x, (uint32_t)r.y, (uint32_t)std::max(1, r.w), (uint32_t)std::max(1, r.h)};
        xcb_configure_window(conn(), win_,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                 XCB_CONFIG_WINDOW_HEIGHT,
                             v);
        xcb_flush(conn());
    }
    void show() override
    {
        const uint32_t above = XCB_STACK_MODE_ABOVE;
        xcb_map_window(conn(), win_);
        xcb_configure_window(conn(), win_, XCB_CONFIG_WINDOW_STACK_MODE, &above);
        xcb_flush(conn());
    }
    void hide() override
    {
        xcb_unmap_window(conn(), win_);
        xcb_flush(conn());
    }

    xcb_window_t win_ = 0;
    CFrame *frame_ = nullptr;
};

} // namespace

std::unique_ptr<PopupWindow> PopupWindow::create(CFrame *editor, const NativeRect &r, int w, int h, double scale,
                                                 CView *content, IPlatformFrameConfig *config)
{
    auto *c = conn();
    if (!c || !window_of(editor)) {
        content->forget();
        return nullptr;
    }
    auto *scr = screen_of(c);
    auto win = std::make_unique<X11PopupWindow>();
    win->win_ = xcb_generate_id(c);
    const uint32_t values[] = {scr->black_pixel, 1u}; // background, override-redirect
    xcb_create_window(c, XCB_COPY_FROM_PARENT, win->win_, scr->root, (int16_t)r.x, (int16_t)r.y,
                      (uint16_t)std::max(1, r.w), (uint16_t)std::max(1, r.h), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      scr->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
    // tells compositors it is a menu (shadow, no animation of a normal window)
    const xcb_atom_t type = atom(c, "_NET_WM_WINDOW_TYPE"), menu = atom(c, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
    if (type != XCB_ATOM_NONE && menu != XCB_ATOM_NONE)
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, win->win_, type, XCB_ATOM_ATOM, 32, 1, &menu);

    auto *frame = new CFrame(CRect(0, 0, w, h), nullptr);
    if (!frame->open(reinterpret_cast<void *>((uintptr_t)win->win_), PlatformType::kX11EmbedWindowID, config)) {
        frame->forget();
        content->forget();
        xcb_destroy_window(c, win->win_);
        xcb_flush(c);
        return nullptr;
    }
    win->frame_ = frame;
    if (scale != 1.0)
        frame->setZoom(scale);
    frame->addView(content);
    // VSTGUI maps its child window only for an XEmbed host; ours is the parent
    if (auto child = window_of(frame))
        xcb_map_window(c, child);
    xcb_flush(c);
    return win;
}

bool popup_screen(CFrame *editor, CPoint at, double content_scale, PopupScreen &out)
{
    auto *c = conn();
    const xcb_window_t w = window_of(editor);
    if (!c || !w)
        return false;
    auto *scr = screen_of(c);
    auto *tr = xcb_translate_coordinates_reply(c, xcb_translate_coordinates(c, w, scr->root, 0, 0), nullptr);
    if (!tr)
        return false;
    const double zoom = editor->getZoom();
    out.anchor_x = tr->dst_x + (int)std::lround(at.x * zoom);
    out.anchor_y = tr->dst_y + (int)std::lround(at.y * zoom);
    free(tr);
    out.scale = content_scale > 0 ? content_scale : 1.0;
    out.work = {0, 0, scr->width_in_pixels, scr->height_in_pixels};
    // the desktop's work area (without panels) when the window manager publishes it; spans all monitors
    const xcb_atom_t wa = atom(c, "_NET_WORKAREA");
    if (wa != XCB_ATOM_NONE) {
        auto *p = xcb_get_property_reply(c, xcb_get_property(c, 0, scr->root, wa, XCB_ATOM_CARDINAL, 0, 4), nullptr);
        if (p && xcb_get_property_value_length(p) >= 16) {
            const uint32_t *v = static_cast<const uint32_t *>(xcb_get_property_value(p));
            if (v[2] > 0 && v[3] > 0)
                out.work = {(int)v[0], (int)v[1], (int)v[2], (int)v[3]};
        }
        free(p);
    }
    return true;
}

bool popup_pointer(CFrame *, int &x, int &y, bool &down)
{
    auto *c = conn();
    if (!c)
        return false;
    auto *p = xcb_query_pointer_reply(c, xcb_query_pointer(c, screen_of(c)->root), nullptr);
    if (!p)
        return false;
    x = p->root_x;
    y = p->root_y;
    down = (p->mask & (XCB_BUTTON_MASK_1 | XCB_BUTTON_MASK_2 | XCB_BUTTON_MASK_3)) != 0;
    free(p);
    return true;
}

uintptr_t popup_focus_token(CFrame *) { return 0; } // a click elsewhere closes the menu; keyboard switches do not

int popup_submenu_delay() { return 400; }

} // namespace vti
