// editorrender.h - draws the original editor layout (editormodel.h) on any canvas and finds the control under the
// mouse. Shared by tools/vtirender (cairo, checks against screenshots of the original) and the plug-in (VSTGUI).
// Rendering rules: docs/formats.md "editor rendering rules".
#pragma once
#include "editormodel.h"
#include <array>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <string>

namespace vti {

class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void *image(const std::string &name) = 0; // a PNG of the user's Access files; nullptr if missing
    virtual int image_width(void *img) = 0;
    virtual int image_height(void *img) = 0;
    // copy the w x h region at (sx, sy) of `img` to (dx, dy) with `alpha`
    virtual void blit(void *img, int sx, int sy, int dx, int dy, int w, int h, double alpha) = 0;
    virtual void fill(int x, int y, int w, int h, Rgba c) = 0;
    virtual void line(double x0, double y0, double x1, double y1, Rgba c) = 0; // 1 pixel wide
    // drawing limited to a rectangle (inside the current clip) until pop_clip; like VSTGUI views, the displays
    // (envelope, filter, oscillators) never draw outside their element
    virtual void push_clip(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
    virtual void pop_clip() {}
};

struct Hit {
    const Element *element = nullptr; // the topmost visible control with a parameter under the point
    int x = 0, y = 0, part = 0;        // its absolute position and effective part
};

class EditorRenderer {
public:
    EditorRenderer(const EditorLayout &layout, const Conditions &cond, const RemoteDevice &dev, const RemoteDevice &ed)
        : layout_(layout), cond_(cond), dev_(dev), ed_(ed)
    {
    }

    ValueFn values;                                 // (path, part) -> value
    std::function<std::string(int part)> patch_name; // a part's patch name
    std::function<std::string(int bank, int program)> bank_patch; // a stored patch's name (bank 0..3 = RAM A..D)
    std::function<bool(int bank, int program)> browser_match;     // category filter / search (false = dimmed)
    // live texts (LCD: "VirusControl/Display Parameter" / "Display Value" / "Display Part"); empty = valuelist
    std::function<std::string(const std::string &param)> live_text;
    // a proxy control's target (the soft knobs: the parameter their destination points to), "" for others; the
    // control then shows the target's value, range and value list like the original (E150)
    std::function<std::string(const std::string &param, int part)> proxy_target;
    std::function<int(int channel)> meter;           // MIDI activity 0..127 per channel (part strip VU meters)
    int part = 0;                                    // the current (panel) part
    int layer = 0;                                   // 0 = everything, 1 = static only, 2 = controls only
    std::vector<const Element *> static_key;          // filled by a layer-1 render: visible conditional statics
    // the visible conditional static elements right now (compare with static_key to know if the cache is valid)
    std::vector<const Element *> static_signature() const;
    double disabled_alpha = 0.4;
    // the element held down by the mouse (drawn with PressedBack)
    const Element *pressed = nullptr;

    // which parameters the last full render read, for partial repaints of value changes (automation, Virus panel):
    // `cond` = read by a show/hide condition (a change can alter the structure -> full repaint), `rects` = the areas
    // of the elements that read it while drawing (knobs, displays, texts). Filled by render() when `deps` is set.
    struct Deps {
        std::unordered_set<std::string> cond;
        std::unordered_map<std::string, std::vector<std::array<int, 4>>> rects;
    };
    Deps *deps = nullptr;
    void render(Canvas &c);
    // one line in an LCD bitmap font, centred in the rectangle (the plug-in's own notes on the original's pages)
    void status_text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h)
    {
        text(c, font, str, x, y, w, h, "Middle Center");
    }
    // repaint area: elements outside it are skipped (with their children); default = everything
    int clip_x0 = -100000, clip_y0 = -100000, clip_x1 = 100000, clip_y1 = 100000;
    Hit hit_test(int x, int y) const;
    // false for inactive controls (the parameter's own condition is false; soft knobs without destination): they are
    // drawn translucent and ignore the mouse
    bool param_active(const std::string &param, int part) const;
    // absolute rectangles (x, y, w, h) of all elements bound to `param` (for partial repaints)
    std::vector<std::array<int, 4>> param_rects(const std::string &param) const;
    // the browser entry (program 0..127) under a point inside a PatchBrowser hit, or -1
    static int browser_entry(const Hit &h, int x, int y);
    std::map<int, int> selected; // bank -> selected program in the browser (drawn inverted)
    std::string value_text(const std::string &param, int v) const;
    std::vector<std::pair<int, std::string>> menu_entries(const std::string &param) const; // (value, text), menu order

private:
    const EditorLayout &layout_;
    const Conditions &cond_;
    const RemoteDevice &dev_, &ed_;
    static int own_part(const Element &e);
    bool dep_cond_ = false;           // render(): values are read by a condition
    std::array<int, 4> dep_rect_{};   // render(): the element drawing itself right now (w = 0: none)
    void dep_read(const std::string &path);
    void draw(Canvas &c, const Element &e, int ox, int oy, int inherited);
    // a frame (sy..sy+fh) of `img`, nine-part tiled to w x h when the bitmap is marked tiled
    void image(Canvas &c, const std::string &name, void *img, int sy, int fh, int x, int y, int w, int h, double alpha);
    void text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h,
              const std::string &align, double alpha = 1.0);
    bool hit(const Element &e, int ox, int oy, int inherited, int px, int py, Hit &out) const;
    void patch_browser(Canvas &c, const Element &e, int x, int y, int bank);
    void envelope(Canvas &c, const Element &e, int x, int y, int part, double alpha);
    void filter_response(Canvas &c, const Element &e, int x, int y, int part);
    void osc_classic(Canvas &c, const Element &e, int x, int y, int part);
    void osc_hypersaw(Canvas &c, const Element &e, int x, int y, int part);
    void arp_edit(Canvas &c, const Element &e, int x, int y, int part);

public:
    // ArpEdit geometry (shared with the editor's mouse handling): step cell left edge and the bars' bottom row
    static void arp_step_geometry(const Element &e, int x, int y, int step, int &cell_x, int &bottom);
    static int arp_step_width(int len); // right border offset from the cell's left edge for a step length
    void digits(Canvas &c, int number, int right, int top);
};

} // namespace vti
