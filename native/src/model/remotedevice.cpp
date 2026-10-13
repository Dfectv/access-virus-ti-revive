// remotedevice.cpp - see remotedevice.h. Format notes: docs/formats.md.
#include "remotedevice.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
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
        for (auto t : c.children("Test")) {
            std::string type = t.attribute("type").value();
            auto kind = type == "inside" ? Condition::Test::Inside
                        : type == "outside" ? Condition::Test::Outside : Condition::Test::Equals;
            k.tests.push_back({kind, num(t, "minvalue", 0), num(t, "maxvalue", 0), t.attribute("param").value()});
        }
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

size_t RemoteDevice::encode_into(const std::string &path, int value, int part, uint8_t *out, size_t cap, int device_id) const
{
    const Parameter *p = find(path);
    const MidiMessage *m = p ? message(p->message) : nullptr;
    if (!m || m->bytes.size() > cap)
        return 0;
    int ctrl = -1;
    for (auto &b : m->bytes)
        for (auto &kv : b.controllers)
            if (kv.second == path)
                ctrl = kv.first;
    if (ctrl < 0)
        return 0;
    size_t n = 0;
    int channel = part & 15;
    for (auto &b : m->bytes) {
        const std::string &c = b.content;
        uint8_t v;
        if (c == "controller")
            v = (uint8_t)(0xB0 | channel);
        else if (c == "polypressure")
            v = (uint8_t)(0xA0 | channel);
        else if (c == "sysexstart")
            v = 0xF0;
        else if (c == "sysexend")
            v = 0xF7;
        else if (c == "byte")
            v = (uint8_t)(b.tag < 0 ? 0 : b.tag);
        else if (c == "deviceid")
            v = (uint8_t)device_id;
        else if (c == "part")
            v = (uint8_t)part;
        else if (c == "controllerid")
            v = (uint8_t)ctrl;
        else if (c == "value")
            v = (uint8_t)(value & 0x7F);
        else
            return 0;
        out[n++] = v;
    }
    return n;
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

bool RemoteDevice::upgrade_single(std::vector<uint8_t> &m, const std::vector<uint8_t> &page_b)
{
    if (m.size() != 524 || m[0] != 0xF0 || m[6] != 0x10)
        return false;
    uint8_t *p = m.data() + 9; // p[0] = format version, p[k] = byte 9 + k
    const int bank = m[7];
    const int v0 = (int8_t)p[0];
    if (v0 < 8 && page_b.size() != 256)
        return false;
    auto set = [&](std::initializer_list<std::pair<int, uint8_t>> l) {
        for (auto &kv : l)
            p[kv.first] = kv.second;
    };
    if (v0 < 1)
        set({{0x90, 0x39}, {0xA1, 0x24}, {0x15, 0x60}, {0x92, 0}, {0x93, 0}, {0x94, 0}, {0x8D, 0}, {0xBE, 0}});
    if (v0 < 2)
        p[0x6D] = (uint8_t)(((p[0x6D] >> 1) & 0x7F) + 0x40);
    if (v0 < 3)
        set({{0x26, 0}, {0x95, 0}, {0xCF, 0}, {0xD0, 0x40}, {0xD1, 0}, {0xD2, 0x40}});
    if (v0 < 4)
        set({{0x99, 1}, {0xE1, 0}, {0xE2, 0x20}});
    if (v0 < 5) {
        set({{0xA2, 0}, {0xA6, 0}, {0xE3, 0}, {0xFB, 0}, {0xFC, 0}, {0x77, 0x40}, {0x27, 0x40}, {0x70, 1}, {0xA9, 0},
             {0xAA, 0x40}, {0xAB, 0x40}, {0xAC, 0x28}});
        if (p[0x44] != 1)
            p[0x47] = 0x40;
        if (p[0x50] != 1)
            p[0x53] = 0x40;
        set({{0xD4, 3}, {0xD5, 0}, {0xD6, 0x24}, {0xD7, 0x70}, {0xD8, 0x30}, {0xD9, 0x40}, {0xDA, 0x7F}});
        for (int k : {0xBE, 0xBF})
            if (p[k]) {
                int c = (int8_t)p[k] + 13;
                p[k] = (uint8_t)(c > 0x52 ? (int8_t)p[k] - 0x45 : c);
            }
    }
    if (v0 < 6) {
        set({{0x0B, 0x7F}, {0xBA, 0}, {0x82, 0}, {0x85, 0x40}, {0x86, 0}});
        if (p[0x81] == 0)
            p[0x91] = 4;
        else if (p[0x91] == 3) {
            p[0x82] = 1;
            p[0x91] = 4;
        }
    }
    if (v0 < 7)
        set({{0xE7, 3}, {0xE8, 0}, {0xE9, 0x40}, {0xEA, 3}, {0xEB, 0}, {0xEC, 0x40}, {0xED, 3}, {0xEE, 0}, {0xEF, 0x40},
             {0xDC, 0x40}, {0xDD, 0x47}, {0xDE, 0x33}, {0xDF, 0x40}, {0xE0, 0x40}, {0xAD, 0x2B}, {0xAE, 0x55}, {0xB3, 0},
             {0xB4, 0}, {0xFE, 0}});
    if (v0 < 8) {
        memcpy(&m[0x10A], page_b.data(), 256); // page B did not exist before version 8
        m[0x20B] = 0xF7;
        bool no_fm = m[0x95] == 0;
        for (int a : {0xC9, 0xCC, 0xD1, 0xF0, 0xF3, 0xF6})
            no_fm = no_fm && m[a] != 0x17;
        for (int a : {0xCA, 0xCD, 0xCF, 0xD2, 0xD4, 0xD6, 0xF1, 0xF4, 0xF7, 0xD8, 0xDA})
            no_fm = no_fm && m[a] != 0x3A;
        if (no_fm)
            m[0x90] = 0x30;
        int c = (int8_t)m[0x79];
        if (c >= 2 && c <= 4) { // old delay types -> the new delay section
            m[0x10C] = m[0x7A], m[0x10B] = (uint8_t)(c - 1), m[0x10D] = m[0x7E], m[0x10E] = m[0x7D], m[0x10F] = m[0x7F];
            m[0x110] = m[0x80], m[0x111] = m[0x7B], m[0x112] = m[0x9D], m[0x113] = m[0x7C];
        } else {
            if (c > 4)
                m[0x79] = (uint8_t)(c - 3);
            m[0x112] = 0, m[0x113] = 0, m[0x111] = 0x1C;
            m[0x10B] = 0x01, m[0x10C] = 0x00, m[0x10D] = 0x02, m[0x10E] = 0x50, m[0x10F] = 0x14, m[0x110] = 0x31;
        }
        if (c == 0 || (c >= 2 && c <= 4)) {
            m[0x9D] = 0;
            m[0x79] = 0x01, m[0x7A] = 0x00, m[0x7B] = 0x40, m[0x7C] = 0x00;
            m[0x7D] = 0x28, m[0x7E] = 0x06, m[0x7F] = 0x01, m[0x80] = 0x40;
        }
        p[0] = 8;
    }
    if ((int8_t)p[0] < 9) {
        if (bank == 0) {
            if (m[0x128] < 4)
                m[0x134] = 0x40;
            if (m[0x12D] < 4)
                m[0x148] = 0x40;
        }
        if ((int8_t)p[0xFE] > 1)
            p[0xFE] = 0;
        if (p[0x11F] < 4)
            p[0x12B] = 0x40;
        if (p[0x124] < 4)
            p[0x13F] = 0x40;
        set({{0x10D, 3}, {0x10E, 4}, {0x10F, 2}, {0x11B, 0}, {0x10B, 0}, {0x112, 0}, {0x147, 0}, {0x148, 0}, {0x114, 0},
             {0x115, 0x7F}, {0x116, 0x40}, {0x117, 0x40}, {0x118, 0x7F}, {0x119, 0x7F}, {0x149, 0x7F}});
        p[0] = 9;
    }
    // versions 10..12 only for edit-buffer dumps (bank 0); stored patches are converted by the firmware
    if (bank == 0 && (int8_t)p[0] < 10) {
        if ((int8_t)p[0xFF] > 1)
            p[0xFF] = 0;
        if ((int8_t)p[0xE4] < 0x12)
            p[0x14A] = 0x7F;
        if ((int8_t)p[0xE4] < 0x14)
            p[0x14B] = 0x40;
        if ((int8_t)p[0x114] < 3)
            p[0x11A] = 0x40;
        p[0] = 10;
    }
    if (bank == 0 && (int8_t)p[0] < 11) {
        set({{0x67, 1}, {0x68, 0x7F}, {0x6F, 0}, {0x80, 0x40}});
        p[0x32] = p[0x26];
        for (int k = 0; k < 6; k++)
            p[0x1F9 + k] = p[0x61 + k];
        p[0x8F] = p[0x81];
        p[0] = 11;
    }
    if (bank == 0 && (int8_t)p[0] < 12) {
        static const uint8_t mod[10] = {0x14, 0x46, 0, 0, 0x46, 0x14, 0x46, 0, 0, 0x46};
        for (int k = 0; k < 10; k++)
            p[0x151 + k] = mod[k];
        p[0] = 12;
    }
    bool changed = (int8_t)p[0] != v0;
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += m[i];
    m[265] = a & 0x7F;
    for (int i = 265; i < 522; i++)
        a += m[i];
    m[522] = a & 0x7F;
    return changed;
}

std::vector<RemoteDevice::Change> RemoteDevice::decode(const std::vector<uint8_t> &m) const
{
    std::vector<Change> out;
    if (by_ctl_.empty()) { // built lazily: (message id, controller index) -> parameters
        auto *self = const_cast<RemoteDevice *>(this);
        for (auto &p : parameters)
            if (p.controller >= 0 && !p.message.empty())
                self->by_ctl_[{p.message, p.controller}].push_back(p.path);
    }
    std::string id;
    int ctl = -1, part = 0, value = 0;
    if (m.size() == 3 && (m[0] & 0xF0) == 0xB0)
        id = "cc", ctl = m[1], part = m[0] & 15, value = m[2];
    else if (m.size() == 3 && (m[0] & 0xF0) == 0xA0)
        id = "pp", ctl = m[1], part = m[0] & 15, value = m[2];
    else if (m.size() == 11 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33 && m[10] == 0xF7) {
        for (auto &msg : messages) // the parameter SysEx template whose byte 6 is this page
            if (msg.bytes.size() == 11 && msg.bytes[6].tag == m[6])
                id = msg.id;
        ctl = m[8], part = m[7], value = m[9];
    }
    auto it = by_ctl_.find({id, ctl});
    if (it != by_ctl_.end())
        for (auto &path : it->second)
            out.push_back({path, part, value});
    return out;
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
