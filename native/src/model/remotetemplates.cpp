#include "model/remotetemplates.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace vti {

// record layout (E196): F0 00 20 33 01 10 13 <template> then a name record: 20 <name 9..24> <flags 25> 00 F7, a knob
// record: <knob> <name 9..18> <behaviour low 19, high 20> 21..22 <MIDI string 23..62> 00 F7
static constexpr int kNameAt = 9, kNameLen = 16, kFlagsAt = 25, kKnobNameLen = 10, kBehaviourAt = 19, kMidiAt = 23;
static constexpr size_t kNameRec = 28, kOldNameRec = 27, kKnobRec = 65;
static const uint8_t kHead[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13};

static std::string trimmed(const std::string &s)
{
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::string text(const std::vector<uint8_t> &r, int at, int len)
{
    std::string s;
    for (int i = 0; i < len; i++)
        s += r[at + i] >= 32 && r[at + i] < 127 ? (char)r[at + i] : ' ';
    return trimmed(s);
}

// the original pads with spaces and copies at most `len` characters (strncpy)
static void put_text(std::vector<uint8_t> &r, int at, int len, const std::string &s)
{
    for (int i = 0; i < len; i++) {
        unsigned char c = i < (int)s.size() ? (unsigned char)s[i] : ' ';
        r[at + i] = c >= 32 && c < 127 ? c : ' ';
    }
}

static bool ours(const std::vector<uint8_t> &r)
{
    return r.size() > sizeof kHead && std::equal(kHead, kHead + sizeof kHead, r.begin()) && r.back() == 0xF7;
}

static bool valid(int t, int k = 0)
{
    return t >= 0 && t < RemoteTemplates::kTemplates && k >= 0 && k < RemoteTemplates::kKnobs;
}

RemoteTemplates::RemoteTemplates()
{
    for (int t = 0; t < kTemplates; t++) {
        auto &n = names_[t] = std::vector<uint8_t>(kHead, kHead + sizeof kHead);
        n.resize(kNameRec, 0);
        n[7] = (uint8_t)t;
        n[8] = 0x20;
        put_text(n, kNameAt, kNameLen, "Init Template"); // the binary's " Init Template   " includes byte 8
        n[kNameRec - 1] = 0xF7;
        for (int k = 0; k < kKnobs; k++) {
            auto &r = knobs_[t][k] = std::vector<uint8_t>(kHead, kHead + sizeof kHead);
            r.resize(kKnobRec, 0);
            r[7] = (uint8_t)t;
            r[8] = (uint8_t)k;
            put_text(r, kNameAt, kKnobNameLen, "");
            r[kMidiAt] = 0x0E; // the original's default MIDI string (encoding open, E196)
            r[kMidiAt + 1] = 0x0F;
            r[kKnobRec - 1] = 0xF7;
        }
    }
}

std::string RemoteTemplates::name(int t) const
{
    return valid(t) ? text(names_[t], kNameAt, kNameLen) : std::string();
}

bool RemoteTemplates::channel_enabled(int t) const
{
    return valid(t) && (names_[t][kFlagsAt] & 1);
}

std::string RemoteTemplates::knob_name(int t, int k) const
{
    return valid(t, k) ? text(knobs_[t][k], kNameAt, kKnobNameLen) : std::string();
}

int RemoteTemplates::knob_behaviour(int t, int k) const
{
    if (!valid(t, k))
        return 0;
    const auto &r = knobs_[t][k];
    return (r[kBehaviourAt] & 0x0F) | (r[kBehaviourAt + 1] & 0x0F) << 4;
}

void RemoteTemplates::set_name(int t, const std::string &name)
{
    if (valid(t))
        put_text(names_[t], kNameAt, kNameLen, name);
}

void RemoteTemplates::set_channel_enabled(int t, bool on)
{
    if (valid(t))
        names_[t][kFlagsAt] = (uint8_t)((names_[t][kFlagsAt] & 0x7E) | (on ? 1 : 0));
}

void RemoteTemplates::set_knob_name(int t, int k, const std::string &name)
{
    if (valid(t, k))
        put_text(knobs_[t][k], kNameAt, kKnobNameLen, name);
}

void RemoteTemplates::set_knob_behaviour(int t, int k, int value)
{
    if (!valid(t, k))
        return;
    knobs_[t][k][kBehaviourAt] = (uint8_t)(value & 0x0F);
    knobs_[t][k][kBehaviourAt + 1] = (uint8_t)((value >> 4) & 0x0F);
}

const std::vector<uint8_t> &RemoteTemplates::name_record(int t) const
{
    static const std::vector<uint8_t> none;
    return valid(t) ? names_[t] : none;
}

const std::vector<uint8_t> &RemoteTemplates::knob_record(int t, int k) const
{
    static const std::vector<uint8_t> none;
    return valid(t, k) ? knobs_[t][k] : none;
}

bool RemoteTemplates::take_record(const std::vector<uint8_t> &r)
{
    if (!ours(r) || r[7] >= kTemplates)
        return false;
    if (r.size() == kKnobRec) {
        if (r[8] >= kKnobs)
            return false;
        knobs_[r[7]][r[8]] = r;
        return true;
    }
    if ((r.size() != kNameRec && r.size() != kOldNameRec) || r[8] != 0x20)
        return false;
    std::vector<uint8_t> n = r;
    if (n.size() == kOldNameRec) // no flags byte yet: a cleared one goes in after the name (0x18008b8d0)
        n.insert(n.begin() + kFlagsAt, 0);
    names_[r[7]] = n;
    return true;
}

static void put_be32(std::vector<uint8_t> &out, size_t v)
{
    for (int s : {24, 16, 8, 0})
        out.push_back((uint8_t)(v >> s));
}

std::vector<uint8_t> RemoteTemplates::cache() const
{
    std::vector<uint8_t> body;
    auto add = [&body](const std::vector<uint8_t> &r) {
        put_be32(body, r.size());
        body.insert(body.end(), r.begin(), r.end());
    };
    for (int t = 0; t < kTemplates; t++) {
        add(names_[t]);
        for (auto &k : knobs_[t])
            add(k);
    }
    std::vector<uint8_t> out = {'M', 'I', 'D', 'I'};
    put_be32(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

int RemoteTemplates::load_cache(const std::vector<uint8_t> &d)
{
    static const uint8_t magic[] = {'M', 'I', 'D', 'I'};
    if (d.size() < 8 || !std::equal(magic, magic + 4, d.begin()))
        return 0;
    int taken = 0;
    for (size_t pos = 8; pos + 4 <= d.size();) {
        size_t n = (size_t)d[pos] << 24 | (size_t)d[pos + 1] << 16 | (size_t)d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        if (n > d.size() - pos) // a cut file: the complete records before the cut
            break;
        taken += take_record(std::vector<uint8_t>(d.begin() + pos, d.begin() + pos + n));
        pos += n;
    }
    return taken;
}

bool RemoteTemplates::load_file(const std::string &path)
{
    std::vector<uint8_t> d;
    if (FILE *f = fopen(path.c_str(), "rb")) {
        uint8_t buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
            d.insert(d.end(), buf, buf + n);
        fclose(f);
    }
    return load_cache(d) > 0;
}

bool RemoteTemplates::save_file(const std::string &path) const
{
    const std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    auto d = cache();
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok)
        std::filesystem::rename(tmp, path, ec); // another instance reading meanwhile sees the old or the new file
    if (!ok || ec)
        std::filesystem::remove(tmp, ec);
    return ok && !ec;
}

std::string remote_text(const std::string &param, const RemoteTemplates &t, int selected, int knob)
{
    const int sel = selected < 0 ? 0 : selected; // nothing selected yet: the original shows template 0
    if (param == "Remote Templates/Template Name")
        return t.name(sel);
    if (param == "Remote Templates/MIDI Channel Behaviour")
        return t.channel_enabled(sel) ? "Enable MIDI Channel" : "Disable MIDI channel";
    if (param == "Remote Templates/Knob Name")
        return t.knob_name(sel, knob);
    return {};
}

bool remote_set_text(const std::string &param, RemoteTemplates &t, int selected, int knob, const std::string &text)
{
    const int sel = selected < 0 ? 0 : selected;
    if (param == "Remote Templates/Template Name")
        t.set_name(sel, text);
    else if (param == "Remote Templates/Knob Name")
        t.set_knob_name(sel, knob, text);
    else
        return false;
    return true;
}

} // namespace vti
