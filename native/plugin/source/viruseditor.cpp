// viruseditor.cpp - see viruseditor.h.
#include "viruseditor.h"
#include "virusplugin.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
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

class EditorCanvasView : public CView, public IControlListener {
public:
    EditorCanvasView(VirusPlugin *plugin, const CRect &r)
        : CView(r), plugin_(plugin), renderer_(plugin->layout(), plugin->conditions(), plugin->device(), plugin->editor_device()),
          canvas_(plugin->files(), bitmaps_)
    {
        renderer_.values = [this](const std::string &path, int part) { return plugin_->ui_value(path, part); };
        renderer_.patch_name = [this](int part) { return plugin_->patch_name(part); };
        renderer_.bank_patch = [this](int bank, int prog) { return plugin_->bank_patch(bank, prog); };
        renderer_.live_text = [this](const std::string &p) { return plugin_->live_text(p); };
        renderer_.browser_match = [this](int bank, int prog) { return plugin_->browser_match(bank, prog); };
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
        if (e.type == "NameEdit" && event.clickCount >= 2) { // rename the part's patch
            edit_name(h);
            event.consumed = true;
            return;
        }
        if (e.type == "PatchBrowser") { // click selects, double click loads into the current part
            int prog = EditorRenderer::browser_entry(h, (int)event.mousePosition.x, (int)event.mousePosition.y);
            int bank = plugin_->ui_value(e.get("Parameter"), -1);
            if (prog >= 0) {
                renderer_.selected[bank] = prog;
                if (event.clickCount >= 2)
                    plugin_->load_patch(bank, prog, plugin_->ui_value(plugin_->layout().frame_parameter, -1));
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (e.type == "IncButton" || e.type == "DecButton") { // step the value (browser banks, categories ...)
            int lo = 0, hi = 3;
            if (const Parameter *p = plugin_->conditions().find(param))
                lo = p->min, hi = p->max;
            else if (param == "Repository Actions/Category Filter")
                hi = 22;
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

    // IControlListener: the name edit was committed
    void valueChanged(CControl *c) override
    {
        if (auto *te = dynamic_cast<CTextEdit *>(c)) {
            std::string t = te->getText().getString();
            if (editing_search_)
                plugin_->set_search(t);
            else
                plugin_->rename_part(edit_part_, t);
            if (auto *f = getFrame()) // remove the edit field after the current event
                f->doAfterEventProcessing([f, te]() { f->removeView(te); });
            invalid();
        }
    }

private:
    int edit_part_ = 0;
    bool editing_search_ = false;

    // LOAD: RAM A..D -> patch -> into the part; SAVE: RAM A..D -> slot -> store the part's patch there
    void patch_menu(const Hit &h, const CPoint &where, bool save)
    {
        // RAM A..D > 0-15, 16-31, ... > patches (a 128-entry list would not fit on the screen)
        auto *menu = new COptionMenu();
        auto groups = std::make_shared<std::map<COptionMenu *, std::pair<int, int>>>(); // submenu -> bank, first
        for (int b = 0; b < 4; b++) {
            auto *bm = new COptionMenu();
            for (int g = 0; g < 8; g++) {
                auto *gm = new COptionMenu();
                for (int p = g * 16; p < g * 16 + 16; p++) {
                    char t[40];
                    snprintf(t, sizeof t, "%3d  %s", p, plugin_->bank_patch(b, p).c_str());
                    gm->addEntry(t);
                }
                char gt[16];
                snprintf(gt, sizeof gt, "%d - %d", g * 16, g * 16 + 15);
                bm->addEntry(gm, gt);
                (*groups)[gm] = {b, g * 16};
                gm->forget();
            }
            menu->addEntry(bm, std::string("RAM ") + (char)('A' + b));
            bm->forget();
        }
        int part = h.part;
        menu->popup(getFrame(), where, [this, part, save, groups](COptionMenu *m) {
            int32_t idx = -1;
            COptionMenu *sub = m->getLastItemMenu(idx);
            auto it = groups->find(sub);
            if (it == groups->end() || idx < 0)
                return;
            int bank = it->second.first, prog = it->second.second + idx;
            if (save)
                plugin_->store_patch(part, bank, prog);
            else
                plugin_->load_patch(bank, prog, part);
            invalid();
        });
        menu->forget();
    }

    void bank_menu(const Hit &h, const CPoint &where)
    {
        auto *menu = new COptionMenu();
        for (int b = 0; b < 4; b++)
            menu->addEntry(std::string("RAM ") + (char)('A' + b));
        std::string param = h.element->get("Parameter");
        menu->popup(getFrame(), where, [this, param](COptionMenu *m) {
            if (m->getLastResult() >= 0)
                plugin_->ui_set(param, -1, m->getLastResult());
            invalid();
        });
        menu->forget();
    }

    void category_menu(const Hit &h, const CPoint &where)
    {
        auto *menu = new COptionMenu();
        menu->addEntry("All");
        for (int c = 1; c <= 22; c++)
            menu->addEntry(renderer_.value_text("Categories/Category 1", c).c_str());
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
        editing_search_ = true;
        CRect r(h.x, h.y, h.x + std::max(120, h.element->w), h.y + h.element->h);
        auto *te = new CTextEdit(r, this, 0, plugin_->search().c_str());
        te->setBackColor(CColor(20, 30, 60, 255));
        te->setFontColor(CColor(255, 255, 255, 255));
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
        te->setBackColor(CColor(20, 30, 60, 255));
        te->setFontColor(CColor(255, 255, 255, 255));
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
