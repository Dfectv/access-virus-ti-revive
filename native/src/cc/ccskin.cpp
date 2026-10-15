// ccskin.cpp - see ccskin.h.
#include "ccskin.h"
#include <cstdlib>
#include <pugixml.hpp>

namespace vti::cc {

std::string skin_bitmap_name(const std::string &ref)
{
    size_t s = ref.find_last_of("/\\");
    return s == std::string::npos ? ref : ref.substr(s + 1);
}

static uint32_t color(const char *hex, uint32_t def)
{
    if (!hex || !*hex)
        return def;
    return (uint32_t)strtoul(hex, nullptr, 16);
}

static void parse_children(const pugi::xml_node &parent, int ox, int oy, std::vector<SkinNode> &out)
{
    for (auto n : parent.children()) {
        std::string tag = n.name();
        if (tag != "container" && tag != "control")
            continue;
        SkinNode s;
        s.container = tag == "container";
        s.type = s.container ? "" : n.attribute("type").as_string();
        s.x = ox + n.attribute("left").as_int();
        s.y = oy + n.attribute("top").as_int();
        s.w = n.attribute("width").as_int();
        s.h = n.attribute("height").as_int();
        s.fore = color(n.attribute("forecolor").as_string(), 0xffffffffu);
        s.back = color(n.attribute("backcolor").as_string(), 0);
        s.parameter = n.attribute("parameter").as_string();
        s.expression = n.attribute("expression").as_string();
        s.bitmap = skin_bitmap_name(n.attribute("bitmap").as_string());
        s.background = skin_bitmap_name(n.attribute("background").as_string());
        s.value = n.attribute("value").as_int();
        if (s.container)
            parse_children(n, s.x, s.y, s.children);
        out.push_back(std::move(s));
    }
}

bool parse_skin(const std::vector<uint8_t> &xml, SkinLayout &out, std::string &error)
{
    pugi::xml_document doc;
    auto r = doc.load_buffer(xml.data(), xml.size());
    if (!r) {
        error = std::string("Control Center layout: ") + r.description();
        return false;
    }
    auto frame = doc.child("editor").child("frame");
    if (!frame) {
        error = "Control Center layout: no <editor><frame>";
        return false;
    }
    out = SkinLayout{};
    out.width = frame.attribute("width").as_int();
    out.height = frame.attribute("height").as_int();
    parse_children(frame, 0, 0, out.nodes);
    return out.width > 0 && out.height > 0;
}

} // namespace vti::cc
