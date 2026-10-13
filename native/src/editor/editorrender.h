// editorrender.h - draws the original editor layout (editormodel.h) on any canvas and finds the control under the
// mouse. Shared by tools/vtirender (cairo, checks against screenshots of the original) and the plug-in (VSTGUI).
// Rendering rules: docs/formats.md "editor rendering rules".
#pragma once
#include "editormodel.h"
#include <functional>
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
    int part = 0;                                    // the current (panel) part
    double disabled_alpha = 0.4;
    // the element held down by the mouse (drawn with PressedBack)
    const Element *pressed = nullptr;

    void render(Canvas &c);
    Hit hit_test(int x, int y) const;
    std::string value_text(const std::string &param, int v) const;

private:
    const EditorLayout &layout_;
    const Conditions &cond_;
    const RemoteDevice &dev_, &ed_;
    void draw(Canvas &c, const Element &e, int ox, int oy, int inherited);
    // a frame (sy..sy+fh) of `img`, nine-part tiled to w x h when the bitmap is marked tiled
    void image(Canvas &c, const std::string &name, void *img, int sy, int fh, int x, int y, int w, int h, double alpha);
    void text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h,
              const std::string &align, double alpha = 1.0);
    bool hit(const Element &e, int ox, int oy, int inherited, int px, int py, Hit &out) const;
    void patch_browser(Canvas &c, const Element &e, int x, int y, int bank);
    void digits(Canvas &c, int number, int right, int top);
};

} // namespace vti
