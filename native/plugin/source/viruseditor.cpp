// viruseditor.cpp - see viruseditor.h.
#include "viruseditor.h"
#include "virusplugin.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <algorithm>
#include <cmath>
#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#endif

using namespace VSTGUI;

namespace vti {

// Canvas on a VSTGUI draw context; bitmaps are created once from the user's PNGs.
class VstguiCanvas : public Canvas {
public:
    VstguiCanvas(const AccessFiles &files, std::map<std::string, SharedPointer<CBitmap>> &cache)
        : files_(files), cache_(cache)
    {
    }
    CDrawContext *ctx = nullptr;

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

private:
    const AccessFiles &files_;
    std::map<std::string, SharedPointer<CBitmap>> &cache_;
};

class EditorCanvasView : public CView {
public:
    EditorCanvasView(VirusPlugin *plugin, const CRect &r)
        : CView(r), plugin_(plugin), renderer_(plugin->layout(), plugin->conditions(), plugin->device(), plugin->editor_device()),
          canvas_(plugin->files(), bitmaps_)
    {
        renderer_.values = [this](const std::string &path, int part) { return plugin_->ui_value(path, part); };
        renderer_.patch_name = [this](int part) { return plugin_->patch_name(part); };
        renderer_.bank_patch = [this](int bank, int prog) { return plugin_->bank_patch(bank, prog); };
        // device changes (knobs on the Virus, automation) repaint the view
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) {
            if (plugin_->ui_changed())
                invalid();
        }, 50);
    }

    void draw(CDrawContext *ctx) override
    {
        renderer_.part = plugin_->ui_value(plugin_->layout().frame_parameter, -1);
        canvas_.ctx = ctx;
        renderer_.render(canvas_);
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
        if (!h.element)
            return;
        const Element &e = *h.element;
        const std::string &param = e.get("Parameter");
        drag_ = h;
        drag_y_ = event.mousePosition.y;
        drag_value_ = plugin_->ui_value(param, h.part);
        if (e.type == "PatchBrowser") { // click selects, double click loads into the current part
            int prog = EditorRenderer::browser_entry(h, (int)event.mousePosition.x, (int)event.mousePosition.y);
            const std::string &bp = e.get("Parameter");
            int bank = bp.size() > 8 ? bp[8] - '0' : 0;
            if (prog >= 0) {
                renderer_.selected[bank] = prog;
                if (event.clickCount >= 2)
                    plugin_->load_patch(bank, prog, plugin_->ui_value(plugin_->layout().frame_parameter, -1));
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (e.type == "Menu" || e.type == "HierarchicMenu") {
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
        if (e.type.find("Knob") == std::string::npos && e.type.find("Slider") == std::string::npos)
            return;
        const std::string &param = e.get("Parameter");
        const Parameter *p = plugin_->conditions().find(param);
        int lo = p ? p->min : 0, hi = p ? p->max : 127;
        double per_pixel = (hi - lo) / 200.0; // full range over 200 pixels, like a VSTGUI knob
        int v = (int)std::lround(drag_value_ + (drag_y_ - event.mousePosition.y) * per_pixel);
        plugin_->ui_set(param, drag_.part, std::max(lo, std::min(hi, v)));
        invalid();
        event.consumed = true;
    }

    void onMouseUpEvent(MouseUpEvent &event) override
    {
        drag_ = {};
        renderer_.pressed = nullptr;
        invalid();
        event.consumed = true;
    }

private:
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
        for (int v = p->min; v <= p->max; v++) {
            std::string t = renderer_.value_text(param, v);
            if (t.empty())
                continue;
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
    double drag_y_ = 0;
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
