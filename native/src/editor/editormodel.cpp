// editormodel.cpp - see editormodel.h.
#include "editormodel.h"
#include <algorithm>
#include <pugixml.hpp>

namespace vti {

static Rgba color(const pugi::xml_node &n)
{
    return {(uint8_t)n.attribute("red").as_int(), (uint8_t)n.attribute("green").as_int(),
            (uint8_t)n.attribute("blue").as_int(), (uint8_t)n.attribute("alpha").as_int(255)};
}

static void read_element(const pugi::xml_node &n, Element &e)
{
    e.type = n.name();
    for (auto a : n.attributes())
        e.attr[a.name()] = a.value();
    for (auto c : n.children()) {
        std::string name = c.name();
        if (name == "BackColor")
            e.back = color(c);
        else if (name == "ForeColor")
            e.fore = color(c);
        else if (name == "Size") {
            e.x = c.attribute("left").as_int(), e.y = c.attribute("top").as_int();
            e.w = c.attribute("width").as_int(), e.h = c.attribute("height").as_int();
        } else if (c.type() == pugi::node_element) {
            e.children.emplace_back();
            read_element(c, e.children.back());
        }
    }
}

bool EditorLayout::load(const std::vector<uint8_t> &xml, std::string &error)
{
    pugi::xml_document doc;
    auto r = doc.load_buffer(xml.data(), xml.size());
    if (!r) {
        error = std::string("editor.xml: ") + r.description();
        return false;
    }
    auto root = doc.child("Editor");
    for (auto b : root.child("Bitmaps").children()) {
        bitmaps[b.name()] = b.attribute("Name").value();
        if (b.attribute("Tile").as_bool()) {
            auto in = b.child("TileInsetRect");
            tiles[b.attribute("Name").value()] = {in.attribute("left").as_int(), in.attribute("top").as_int(),
                                                  in.attribute("right").as_int(), in.attribute("bottom").as_int()};
        }
    }
    auto frame = root.child("Frame");
    width = frame.attribute("width").as_int(), height = frame.attribute("height").as_int();
    frame_parameter = frame.attribute("parameter").value();
    for (auto c : frame.children()) {
        if (c.type() != pugi::node_element)
            continue;
        elements.emplace_back();
        read_element(c, elements.back());
    }
    if (!width || elements.empty()) {
        error = "editor.xml: no Frame";
        return false;
    }
    return true;
}

const Parameter *Conditions::find(const std::string &path) const
{
    const Parameter *p = dev_.find(path);
    return p ? p : ed_.find(path);
}

int Conditions::default_value(const std::string &path) const
{
    const Parameter *p = find(path);
    return p ? p->def : 0;
}

bool Conditions::eval(const std::string &name, const ValueFn &values, int part) const
{
    if (name.empty())
        return true;
    // conditions the original evaluates in code (not in the XML files)
    if (name == "VirusControl/Browser/Category Filter Selected")
        return values("Repository Actions/Category Filter", -1) != 0;
    if (name == "VirusControl/Browser/Category Filter Not Selected")
        return values("Repository Actions/Category Filter", -1) == 0;
    if (name == "Internal System Info/Display Recall Progress")
        return false;
    bool known = false;
    for (const RemoteDevice *d : {&ed_, &dev_}) {
        auto range = d->conditions.equal_range(name);
        for (auto it = range.first; it != range.second; ++it) {
            known = true;
            const Condition &c = it->second;
            bool r = !c.any; // "or": start false, "and": start true
            for (auto &t : c.tests) {
                const Parameter *p = find(t.param);
                int pt = p && p->part == "none" ? -1 : part;
                bool h = t.holds(values(t.param, pt));
                r = c.any ? (r || h) : (r && h);
            }
            if (c.invert)
                r = !r;
            if (r)
                return true;
        }
    }
    return !known;
}

std::vector<std::string> Conditions::inputs(const std::string &name) const
{
    std::vector<std::string> out;
    for (const RemoteDevice *d : {&ed_, &dev_}) {
        auto range = d->conditions.equal_range(name);
        for (auto it = range.first; it != range.second; ++it)
            for (auto &t : it->second.tests)
                if (std::find(out.begin(), out.end(), t.param) == out.end())
                    out.push_back(t.param);
    }
    return out;
}

} // namespace vti
