// remotedevice.cpp - see remotedevice.h. Format notes: docs/formats.md.
#include "remotedevice.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <pugixml.hpp>

namespace vti {

static int num(const pugi::xml_node &n, const char *a, int def)
{
    pugi::xml_attribute at = n.attribute(a);
    return at ? std::atoi(at.value()) : def;
}

std::string Valuelist::text(int v) const
{
    for (auto &e : entries)
        if (e.index == v)
            return e.text;
    return std::to_string(v);
}

bool RemoteDevice::load(const std::vector<uint8_t> &xml, std::string &error)
{
    pugi::xml_document doc;
    // remotedevice.xml declares ISO-8859-1
    pugi::xml_parse_result r = doc.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_latin1);
    if (!r) {
        error = std::string("remotedevice.xml: ") + r.description();
        return false;
    }
    pugi::xml_node root = doc.child("Remotedevice");
    if (!root) {
        error = "remotedevice.xml: no <Remotedevice>";
        return false;
    }
    device = root.attribute("device").value();
    version = root.attribute("version").value();

    for (auto set : root.child("ParameterSets").children("ParameterSet")) {
        std::string group = set.attribute("name").value();
        for (auto p : set.children("Parameter")) {
            Parameter q;
            q.group = group;
            q.name = p.attribute("name").value();
            q.path = group + "/" + q.name;
            q.min = num(p, "minValue", 0);
            q.max = num(p, "maxValue", 127);
            q.def = num(p, "defaultValue", 0);
            q.valuelist = p.attribute("valuelist").value();
            q.part = p.attribute("part").value();
            q.condition = p.attribute("condition").value();
            q.message = p.attribute("defaultmessage").value();
            q.controller = num(p, "controllerindex", -1);
            q.shortname = p.attribute("shortname").value();
            q.displayname = p.attribute("displayname").value();
            q.automatable = std::string(p.attribute("automatable").value()) == "true";
            q.performance = (bool)p.attribute("performanceCtrl");
            index_[q.path] = parameters.size();
            parameters.push_back(q);
        }
    }
    for (auto v : root.child("Valuelists").children("Valuelist")) {
        Valuelist l;
        l.name = v.attribute("name").value();
        l.mapped = std::string(v.attribute("type").value()) == "mapped";
        for (auto e : v.children("Entry"))
            l.entries.push_back({num(e, "index", 0), num(e, "value", -1), e.text().get()});
        valuelists[l.name] = l;
    }
    for (auto c : root.child("Conditionsets").children("Condition")) {
        Condition k;
        k.name = c.attribute("name").value();
        k.any = std::string(c.attribute("operand").value()) != "and";
        k.invert = std::string(c.attribute("result").value()) == "invert";
        for (auto t : c.children("Test"))
            k.tests.push_back({std::string(t.attribute("type").value()) == "inside", num(t, "minvalue", 0),
                               num(t, "maxvalue", 0), t.attribute("param").value()});
        conditions.emplace(k.name, k);
    }
    for (auto m : root.child("MIDIMessages").children("MIDIMessage")) {
        MidiMessage mm;
        mm.id = m.attribute("id").value();
        mm.name = m.attribute("name").value();
        for (auto b : m.children("byte")) {
            MessageByte mb;
            mb.index = num(b, "index", 0);
            mb.content = b.attribute("content").value();
            mb.tag = num(b, "tag", -1);
            mb.mask = num(b, "mask", -1);
            mb.shift = num(b, "shift", 0);
            mb.part = num(b, "part", -1);
            mb.method = b.attribute("method").value();
            mb.start = num(b, "startindex", -1);
            mb.stop = num(b, "stopindex", -1);
            for (auto p : b.children("parameter"))
                mb.controllers.push_back({num(p, "mapindex", 0), p.attribute("content").value()});
            mm.bytes.push_back(mb);
        }
        messages.push_back(mm);
    }
    for (auto m : root.child("SoftknobMapping").children("Mapping")) {
        size_t i = (size_t)num(m, "index", 0);
        if (softknobs.size() <= i)
            softknobs.resize(i + 1);
        softknobs[i] = m.attribute("parameter").value();
    }
    for (auto g : root.child("Initialisation").children("GlobalInit"))
        global_init.push_back({g.attribute("param").value(), g.attribute("value").value()});
    for (auto g : root.child("Initialisation").children("ParamInit"))
        param_init.push_back({g.attribute("param").value(), g.attribute("value").value(), num(g, "part", 0)});
    return true;
}

const Parameter *RemoteDevice::find(const std::string &path) const
{
    auto it = index_.find(path);
    return it == index_.end() ? nullptr : &parameters[it->second];
}

const MidiMessage *RemoteDevice::message(const std::string &id) const
{
    for (auto &m : messages)
        if (m.id == id)
            return &m;
    return nullptr;
}

std::vector<uint8_t> RemoteDevice::encode(const std::string &path, int value, int part, int device_id) const
{
    const Parameter *p = find(path);
    const MidiMessage *m = p ? message(p->message) : nullptr;
    if (!m)
        return {};
    int ctrl = -1; // the parameter's id inside this message (map index of the controllerid byte)
    for (auto &b : m->bytes)
        for (auto &kv : b.controllers)
            if (kv.second == path)
                ctrl = kv.first;
    if (ctrl < 0)
        return {};
    std::vector<uint8_t> out;
    int channel = part & 15;
    for (auto &b : m->bytes) {
        const std::string &c = b.content;
        if (c == "controller")
            out.push_back((uint8_t)(0xB0 | channel));
        else if (c == "polypressure")
            out.push_back((uint8_t)(0xA0 | channel));
        else if (c == "sysexstart")
            out.push_back(0xF0);
        else if (c == "sysexend")
            out.push_back(0xF7);
        else if (c == "byte")
            out.push_back((uint8_t)(b.tag < 0 ? 0 : b.tag));
        else if (c == "deviceid")
            out.push_back((uint8_t)device_id);
        else if (c == "part")
            out.push_back((uint8_t)part);
        else if (c == "controllerid")
            out.push_back((uint8_t)ctrl);
        else if (c == "value")
            out.push_back((uint8_t)(value & 0x7F));
        else
            return {}; // dumps etc. are not single-parameter transports
    }
    return out;
}

static bool is_field(const MessageByte &b)
{
    return b.content.find('/') != std::string::npos;
}

static std::string field_key(const MessageByte &b)
{
    return b.part < 0 ? b.content : b.content + "@" + std::to_string(b.part);
}

static int checksum(const MessageByte &b, const std::vector<uint8_t> &msg)
{
    unsigned sum = 0; // add7: 7-bit sum of the bytes start..stop (the only method in remotedevice.xml)
    for (int i = b.start; i <= b.stop && i < (int)msg.size(); i++)
        sum += msg[i];
    return sum & 0x7F;
}

size_t RemoteDevice::dump_size(const MidiMessage &m)
{
    size_t n = 0;
    for (auto &b : m.bytes)
        n = std::max(n, (size_t)b.index + 1);
    return n;
}

int RemoteDevice::single_fingerprint(const std::vector<uint8_t> &msg)
{
    if (msg.size() != 524)
        return -1;
    unsigned sum = 0;
    for (size_t i = 13; i <= 521; i++)
        if (i != 265)
            sum += msg[i];
    return (int)(sum & 0x3FFF);
}

int RemoteDevice::value_of(const std::string &path, const std::string &text) const
{
    const Parameter *p = find(path);
    if (!p)
        return -1;
    auto vl = valuelists.find(p->valuelist);
    if (vl != valuelists.end())
        for (auto &e : vl->second.entries)
            if (e.text == text)
                return e.index;
    char *end = nullptr;
    long v = strtol(text.c_str(), &end, 10);
    if (end && *end == 0 && !text.empty()) {
        // numbered lists show 1-based numbers ("01" = MIDI channel 1 = value 0) when the list's first text is "1"/"01"
        if (vl != valuelists.end() && !vl->second.entries.empty()) {
            const auto &first = vl->second.entries.front();
            if (atoi(first.text.c_str()) == 1 && first.index == 0)
                return (int)v - 1;
        }
        return (int)v;
    }
    return -1;
}

std::vector<std::vector<uint8_t>> RemoteDevice::init_messages(int device_id) const
{
    std::vector<std::vector<uint8_t>> out;
    for (auto &g : global_init) {
        int v = value_of(g.first, g.second);
        auto m = v >= 0 ? encode(g.first, v, 0, device_id) : std::vector<uint8_t>{};
        if (!m.empty())
            out.push_back(m);
        else
            fprintf(stderr, "remotedevice: GlobalInit %s = %s not encodable\n", g.first.c_str(), g.second.c_str());
    }
    for (auto &pi : param_init) {
        int v = value_of(pi.param, pi.value);
        auto m = v >= 0 ? encode(pi.param, v, pi.part - 1, device_id) : std::vector<uint8_t>{};
        if (!m.empty())
            out.push_back(m);
        else
            fprintf(stderr, "remotedevice: ParamInit %s = %s not encodable\n", pi.param.c_str(), pi.value.c_str());
    }
    return out;
}

bool RemoteDevice::decode_dump(const MidiMessage &m, const std::vector<uint8_t> &msg, std::map<std::string, int> &values,
                               std::string &error) const
{
    if (msg.size() != dump_size(m)) {
        error = m.id + ": " + std::to_string(msg.size()) + " bytes, expected " + std::to_string(dump_size(m));
        return false;
    }
    for (auto &b : m.bytes) {
        if (!b.method.empty() && checksum(b, msg) != msg[b.index]) {
            error = m.id + ": checksum mismatch at byte " + std::to_string(b.index);
            return false;
        }
        if (b.content == "byte" && b.tag >= 0 && b.index < 7 && msg[b.index] != b.tag) {
            error = m.id + ": header byte " + std::to_string(b.index) + " differs";
            return false;
        }
    }
    for (auto &b : m.bytes)
        if (is_field(b)) {
            int v = msg[b.index];
            if (b.mask >= 0)
                v = (v & b.mask) >> b.shift;
            values[field_key(b)] = v;
        }
    return true;
}

void RemoteDevice::update_dump(const MidiMessage &m, std::vector<uint8_t> &msg, const std::map<std::string, int> &values) const
{
    if (msg.size() < dump_size(m))
        msg.resize(dump_size(m));
    for (auto &b : m.bytes) {
        if (!is_field(b))
            continue;
        auto it = values.find(field_key(b));
        if (it == values.end())
            continue;
        if (b.mask >= 0)
            msg[b.index] = (uint8_t)((msg[b.index] & ~b.mask) | ((it->second << b.shift) & b.mask));
        else
            msg[b.index] = (uint8_t)(it->second & 0x7F);
    }
    for (auto &b : m.bytes)
        if (!b.method.empty())
            msg[b.index] = (uint8_t)checksum(b, msg);
}

bool load_automation(const std::vector<uint8_t> &xml, std::vector<AutomationEntry> &out, std::string &error)
{
    pugi::xml_document doc;
    auto r = doc.load_buffer(xml.data(), xml.size());
    if (!r) {
        error = std::string("automation.xml: ") + r.description();
        return false;
    }
    for (auto a : doc.child("AutomationList").children("Automation")) {
        AutomationEntry e;
        e.path = a.attribute("Parameter").as_string();
        e.part = a.attribute("Part").as_int(1);
        e.id = a.attribute("Index").as_int(-1);
        if (e.id < 0 || e.path.empty())
            continue;
        // keyed by (parameter, part) like the original: a later entry replaces an earlier one (automation.xml lists
        // Filter Bank/Filter Frequency twice for part 4 - id 1574 should be part 3; the original leaves 1574 unmapped)
        auto same = std::find_if(out.begin(), out.end(),
                                 [&](const AutomationEntry &o) { return o.path == e.path && o.part == e.part; });
        if (same != out.end())
            *same = e;
        else
            out.push_back(e);
    }
    return !out.empty();
}

std::string automation_title(const RemoteDevice &dev, const AutomationEntry &e)
{
    const Parameter *p = dev.find(e.path);
    if (p && !p->shortname.empty())
        return p->shortname;
    size_t slash = e.path.rfind('/');
    return slash == std::string::npos ? e.path : e.path.substr(slash + 1);
}

} // namespace vti
