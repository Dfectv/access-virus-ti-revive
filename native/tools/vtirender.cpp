// vtirender - render the original editor's layout offscreen from the user's Access files (R4 check): backgrounds,
// knob filmstrips at the current values, buttons by state, conditions evaluated. Text is not drawn yet.
//   vtirender out.png [--state FILE] [--part N] [--set PATH=VALUE]... [--outline]
// --state takes a plug-in state (formats.md, or vtihost's VTISTATE file) for the device values of the parts.
#include "access/accessfiles.h"
#include "editor/editormodel.h"
#include "model/remotedevice.h"
#include <cairo.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>

using namespace vti;

struct PngCache {
    const AccessFiles &files;
    std::map<std::string, cairo_surface_t *> cache;
    cairo_surface_t *get(const std::string &name)
    {
        if (name.empty())
            return nullptr;
        auto it = cache.find(name);
        if (it != cache.end())
            return it->second;
        const auto &d = files.get(name);
        cairo_surface_t *s = nullptr;
        if (!d.empty()) {
            struct Reader {
                const std::vector<uint8_t> *d;
                size_t pos;
            } rd{&d, 0};
            s = cairo_image_surface_create_from_png_stream(
                [](void *c, unsigned char *out, unsigned int n) {
                    auto *r = (Reader *)c;
                    if (r->pos + n > r->d->size())
                        return CAIRO_STATUS_READ_ERROR;
                    memcpy(out, r->d->data() + r->pos, n);
                    r->pos += n;
                    return CAIRO_STATUS_SUCCESS;
                },
                &rd);
            if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
                cairo_surface_destroy(s);
                s = nullptr;
            }
        }
        if (!s)
            fprintf(stderr, "vtirender: missing bitmap %s\n", name.c_str());
        cache[name] = s;
        return s;
    }
};

struct Renderer {
    cairo_t *cr;
    PngCache &png;
    const Conditions &cond;
    ValueFn values;
    int part;
    bool outline;
    int drawn = 0, hidden = 0;

    // draw frame `index` of a vertical filmstrip whose frames are `fh` high
    void strip(cairo_surface_t *s, int x, int y, int w, int h, int index)
    {
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, s, x, y - index * h);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    std::function<std::string(int part)> patch_name;
    const RemoteDevice *dev = nullptr, *ed = nullptr;

    // bitmap font: vertical strip of 96 glyphs (ASCII 32..127), 6 x 9 pixels each
    void text(const std::string &font, const std::string &str, int x, int y, int w, int h, const std::string &align)
    {
        cairo_surface_t *f = png.get(font);
        if (!f || str.empty())
            return;
        const int gw = 6, gh = cairo_image_surface_get_height(f) / 96;
        int tw = (int)str.size() * gw;
        int tx = align.find("Left") != std::string::npos    ? x
                 : align.find("Right") != std::string::npos ? x + w - tw
                                                            : x + (w - tw) / 2;
        int ty = align.find("Top") != std::string::npos      ? y
                 : align.find("Bottom") != std::string::npos ? y + h - gh
                                                             : y + (h - gh) / 2;
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
        for (size_t i = 0; i < str.size(); i++) {
            int c = (unsigned char)str[i];
            if (c < 32 || c > 127)
                c = '?';
            cairo_save(cr);
            cairo_rectangle(cr, tx + (int)i * gw, ty, gw, gh);
            cairo_clip(cr);
            cairo_set_source_surface(cr, f, tx + (int)i * gw, ty - (c - 32) * gh);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        cairo_restore(cr);
    }

    std::string value_text(const std::string &param, int v)
    {
        const Parameter *p = cond.find(param);
        if (!p)
            return std::to_string(v);
        for (const RemoteDevice *d : {dev, ed}) {
            auto vl = d ? d->valuelists.find(p->valuelist) : decltype(dev->valuelists.end()){};
            if (d && vl != d->valuelists.end()) {
                std::string t = vl->second.text(v);
                if (!t.empty())
                    return t;
            }
        }
        return std::to_string(v);
    }

    void draw(const Element &e, int ox, int oy, int inherited = -1)
    {
        // Part -1 = the enclosing container's part (patch containers per part), else the current part
        int pt = e.part() >= 0 ? e.part() : inherited >= 0 ? inherited : part;
        if (!cond.eval(e.get("Condition"), values, pt)) {
            hidden++;
            return;
        }
        int x = ox + e.x, y = oy + e.y;
        if (getenv("VTIRENDER_AT")) { // debug: elements covering a point "x,y"
            int px = atoi(getenv("VTIRENDER_AT")), py = atoi(strchr(getenv("VTIRENDER_AT"), ',') + 1);
            if (px >= x && px < x + e.w && py >= y && py < y + e.h)
                fprintf(stderr, "at: %s \"%s\" (%d,%d %dx%d) %s %s cond=%s\n", e.type.c_str(), e.get("Name").c_str(), x, y, e.w, e.h,
                        e.get("NormalBack").c_str(), e.get("Parameter").c_str(), e.get("Condition").c_str());
        }
        const std::string &param = e.get("Parameter");
        cairo_surface_t *normal = png.get(e.get("NormalBack"));
        int v = param.empty() ? 0 : values(param, pt);
        const Parameter *p = param.empty() ? nullptr : cond.find(param);
        if (normal) {
            int ih = cairo_image_surface_get_height(normal);
            int frames = e.h > 0 ? std::max(1, ih / e.h) : 1;
            int index = 0;
            if (e.type.find("Knob") != std::string::npos || e.type.find("Slider") != std::string::npos) {
                int lo = p ? p->min : 0, hi = p ? p->max : 127;
                index = hi > lo ? (int)std::lround((double)(v - lo) / (hi - lo) * (frames - 1)) : 0;
            } else if (e.type == "RadioButton") {
                // 2-frame strips: frame 0 = off, frame 1 = selected; PressedBack only while the mouse is down
                bool on = v == atoi(e.get("OnValue").c_str());
                index = frames >= 2 && on ? 1 : 0;
            } else if (e.type == "Button" || e.type == "KickButton") {
                index = frames >= 2 && v ? 1 : 0;
            }
            index = std::max(0, std::min(frames - 1, index));
            if (e.type == "VUSlider") { // level bar up to the value (the current part only), handle drawn below
                normal = nullptr;
                cairo_surface_t *bar = png.get(e.get("NormalBack"));
                cairo_surface_t *hd = png.get(e.get("NormalHandle"));
                int hw = hd ? cairo_image_surface_get_width(hd) : 0;
                int lo = p ? p->min : 0, hi = p ? p->max : 127;
                int pos = hi > lo ? (int)std::lround((double)(v - lo) / (hi - lo) * (e.w - hw)) : 0;
                if (bar) { // 53-frame strip; the current part shows its volume, the others are dark
                    int n = std::max(1, cairo_image_surface_get_height(bar) / e.h);
                    int f = pt == part && hi > lo ? (int)std::lround((double)(v - lo) / (hi - lo) * (n - 1)) : 0;
                    strip(bar, x, y, e.w, e.h, f);
                }
                if (hd) {
                    cairo_set_source_surface(cr, hd, x + pos, y + (e.h - cairo_image_surface_get_height(hd)) / 2);
                    cairo_paint(cr);
                }
                drawn++;
            } else if (frames > 1)
                strip(normal, x, y, e.w, e.h, index);
            else {
                cairo_save(cr);
                cairo_rectangle(cr, x, y, e.w, e.h);
                cairo_clip(cr);
                cairo_set_source_surface(cr, normal, x, y);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            drawn++;
        } else if (e.get("Transparent") == "false" && e.back.a) {
            cairo_set_source_rgba(cr, e.back.r / 255., e.back.g / 255., e.back.b / 255., e.back.a / 255.);
            cairo_rectangle(cr, x, y, e.w, e.h);
            cairo_fill(cr);
        }
        const std::string &font = e.get("ControlFont");
        if (!font.empty()) {
            std::string str;
            if (e.type == "StaticText")
                str = e.get("Name");
            else if (e.type == "NameEdit" && patch_name)
                str = patch_name(pt);
            else if ((e.type == "TextSlider" || e.type == "Menu") && !param.empty())
                str = value_text(param, v);
            text(font, str, x, y, e.w, e.h, e.get("Align"));
        }
        if (outline && e.type != "Container") {
            cairo_set_source_rgba(cr, 1, 0, 1, 0.6);
            cairo_set_line_width(cr, 1);
            cairo_rectangle(cr, x + 0.5, y + 0.5, e.w - 1, e.h - 1);
            cairo_stroke(cr);
        }
        for (auto &c : e.children)
            draw(c, x, y, e.part() >= 0 ? e.part() : inherited);
    }
};

static std::vector<std::vector<uint8_t>> read_state(const char *path)
{
    std::vector<uint8_t> d;
    if (FILE *f = fopen(path, "rb")) {
        for (int c; (c = fgetc(f)) != EOF;)
            d.push_back((uint8_t)c);
        fclose(f);
    }
    if (d.size() > 12 && !memcmp(d.data(), "VTISTATE", 8)) {
        uint32_t n;
        memcpy(&n, d.data() + 8, 4);
        d = std::vector<uint8_t>(d.begin() + 12, d.begin() + 12 + std::min<size_t>(n, d.size() - 12));
    }
    std::vector<std::vector<uint8_t>> recs;
    if (d.size() < 8 || memcmp(d.data(), "MIDI", 4))
        return recs;
    for (size_t pos = 8; pos + 4 <= d.size();) {
        size_t n = (size_t)d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        if (pos + n > d.size())
            break;
        recs.emplace_back(d.begin() + pos, d.begin() + pos + n);
        pos += n;
    }
    return recs;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vtirender out.png [--state FILE] [--part N] [--set PATH=VALUE]... [--outline]\n");
        return 2;
    }
    std::string err;
    auto files = AccessFiles::open("", err);
    RemoteDevice dev, ed;
    EditorLayout layout;
    if (!files || !dev.load(files->get("remotedevice.xml"), err) || !ed.load(files->get("editordevice.xml"), err) ||
        !layout.load(files->get("editor.xml"), err)) {
        fprintf(stderr, "vtirender: %s\n", err.c_str());
        return 1;
    }
    Conditions cond(dev, ed);
    int part = 0;
    bool outline = false;
    std::map<std::string, int> set;
    std::map<int, std::map<std::string, int>> parts; // part -> decoded single dump
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--part") && i + 1 < argc)
            part = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--outline"))
            outline = true;
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            std::string a = argv[++i];
            size_t eq = a.find('=');
            if (eq != std::string::npos)
                set[a.substr(0, eq)] = atoi(a.c_str() + eq + 1);
        } else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            const MidiMessage *t = dev.message("VirusTI_Singledump");
            for (auto &r : read_state(argv[++i])) {
                if (t && r.size() == 524 && r[6] == 0x10 && r[7] == 0)
                    dev.decode_dump(*t, r, parts[r[8]], err);
                else // controllers / parameter SysEx of the state (e.g. CC7 volume per channel)
                    for (auto &c : dev.decode(r))
                        parts[c.part][c.path] = c.value;
            }
        }
    }
    ValueFn values = [&](const std::string &path, int pt) {
        auto s = set.find(path);
        if (s != set.end())
            return s->second;
        if (pt >= 0) {
            auto p = parts.find(pt);
            if (p != parts.end()) {
                auto v = p->second.find(path);
                if (v != p->second.end())
                    return v->second;
            }
        }
        return cond.default_value(path);
    };
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, layout.width, layout.height);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    PngCache png{*files, {}};
    Renderer r{cr, png, cond, values, part, outline};
    r.dev = &dev, r.ed = &ed;
    r.patch_name = [&](int pt) {
        std::string n;
        auto p = parts.find(pt);
        if (p == parts.end())
            return n;
        for (int i = 1; i <= 10; i++) {
            char k[48];
            snprintf(k, sizeof k, "Single Patch Name/Character %02d", i);
            auto c = p->second.find(k);
            n += c == p->second.end() ? ' ' : (char)c->second;
        }
        while (!n.empty() && n.back() == ' ')
            n.pop_back();
        return n;
    };
    for (auto &e : layout.elements)
        r.draw(e, 0, 0);
    cairo_surface_write_to_png(surf, argv[1]);
    printf("%dx%d, %d bitmaps drawn, %d subtrees hidden by conditions -> %s\n", layout.width, layout.height, r.drawn,
           r.hidden, argv[1]);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}
