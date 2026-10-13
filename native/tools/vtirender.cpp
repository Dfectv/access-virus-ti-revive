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

    void draw(const Element &e, int ox, int oy)
    {
        int pt = e.part() < 0 ? part : e.part();
        if (!cond.eval(e.get("Condition"), values, pt)) {
            hidden++;
            return;
        }
        int x = ox + e.x, y = oy + e.y;
        if (getenv("VTIRENDER_AT")) { // debug: elements covering a point "x,y"
            int px = atoi(getenv("VTIRENDER_AT")), py = atoi(strchr(getenv("VTIRENDER_AT"), ',') + 1);
            if (px >= x && px < x + e.w && py >= y && py < y + e.h)
                fprintf(stderr, "at: %s \"%s\" %s %s cond=%s\n", e.type.c_str(), e.get("Name").c_str(),
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
                bool on = v == atoi(e.get("OnValue").c_str());
                cairo_surface_t *pressed = png.get(e.get("PressedBack"));
                if (on && pressed)
                    normal = pressed; // 2-frame strips (normal / mouse-over); frame 0 = the plain state
                else if (frames >= 2 && !pressed)
                    index = on ? 1 : 0;
            } else if (e.type == "Button" || e.type == "KickButton") {
                index = frames >= 2 && v ? 1 : 0;
            }
            index = std::max(0, std::min(frames - 1, index));
            if (frames > 1)
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
        if (outline && e.type != "Container") {
            cairo_set_source_rgba(cr, 1, 0, 1, 0.6);
            cairo_set_line_width(cr, 1);
            cairo_rectangle(cr, x + 0.5, y + 0.5, e.w - 1, e.h - 1);
            cairo_stroke(cr);
        }
        for (auto &c : e.children)
            draw(c, x, y);
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
            for (auto &r : read_state(argv[++i]))
                if (t && r.size() == 524 && r[6] == 0x10 && r[7] == 0)
                    dev.decode_dump(*t, r, parts[r[8]], err);
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
    for (auto &e : layout.elements)
        r.draw(e, 0, 0);
    cairo_surface_write_to_png(surf, argv[1]);
    printf("%dx%d, %d bitmaps drawn, %d subtrees hidden by conditions -> %s\n", layout.width, layout.height, r.drawn,
           r.hidden, argv[1]);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}
