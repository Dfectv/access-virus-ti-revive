// REMOTE page, read-only (E193): the original keeps 32 remote templates in the plug-in (default " Init Template   ",
// constructor 0x1800899b0) and reads none from the Virus; its code defines the page's parameters ("Remote Templates/
// ...", value list "Val MIDI Channel Behaviour": Disable MIDI channel / Enable MIDI Channel).
#include "check.h"
#include "model/remotetemplates.h"
using namespace vti;

TEST(remote_templates_defaults)
{
    RemoteTemplates t;
    CHECK(t.name(0) == "Init Template" && t.name(31) == "Init Template");
    CHECK(t.name(-1).empty() && t.name(32).empty());
    CHECK(remote_text("Remote Templates/Template Name", t, 5, 0) == "Init Template");
    CHECK(remote_text("Remote Templates/Template Name", t, -1, 0) == "Init Template"); // nothing selected: template 0
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 0, 0) == "Disable MIDI channel");
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 0, 1) == "Enable MIDI Channel");
    CHECK(remote_text("Osc/Shape", t, 0, 0).empty());
}

// a template's name from its SysEx name record (command 0x13, byte 8 = 0x20): trimmed like the original shows it
TEST(remote_templates_name_record)
{
    RemoteTemplates t;
    const uint8_t rec[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13, 0x03, 0x20, ' ', 'M', 'y', ' ', 'S', 'y', 'n', 't',
                           'h', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 0x00, 0xF7};
    CHECK(t.take_record(std::vector<uint8_t>(rec, rec + sizeof rec)));
    CHECK(t.name(3) == "My Synth");
    std::vector<uint8_t> other(rec, rec + sizeof rec);
    other[6] = 0x10; // a single dump: not ours
    CHECK(!t.take_record(other));
    other = std::vector<uint8_t>(rec, rec + sizeof rec);
    other[7] = 40; // template out of range
    CHECK(!t.take_record(other));
}

namespace {
// the cache's container: "MIDI", a big-endian total length, then each record behind its big-endian length (E196)
std::vector<uint8_t> container(const std::vector<std::vector<uint8_t>> &recs)
{
    std::vector<uint8_t> body;
    for (auto &r : recs) {
        for (int s : {24, 16, 8, 0})
            body.push_back((uint8_t)(r.size() >> s));
        body.insert(body.end(), r.begin(), r.end());
    }
    std::vector<uint8_t> out = {'M', 'I', 'D', 'I'};
    for (int s : {24, 16, 8, 0})
        out.push_back((uint8_t)(body.size() >> s));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
} // namespace

// the default records: what the original's constructor builds (0x1800899b0) and its cache holds untouched (E196)
TEST(remote_templates_default_records)
{
    RemoteTemplates t;
    const auto &n = t.name_record(7);
    CHECK(n.size() == 28 && n[0] == 0xF0 && n[5] == 0x10 && n[6] == 0x13 && n[7] == 7 && n[8] == 0x20);
    CHECK(std::string(n.begin() + 9, n.begin() + 25) == "Init Template   " && n[25] == 0 && n[26] == 0 && n[27] == 0xF7);
    const auto &k = t.knob_record(7, 31);
    CHECK(k.size() == 65 && k[6] == 0x13 && k[7] == 7 && k[8] == 31 && k[64] == 0xF7);
    CHECK(std::string(k.begin() + 9, k.begin() + 19) == "          " && k[19] == 0 && k[20] == 0);
    CHECK(k[23] == 0x0E && k[24] == 0x0F && k[25] == 0); // the default MIDI string
    CHECK(t.knob_name(7, 31).empty() && t.knob_behaviour(7, 31) == 0 && !t.channel_enabled(7));
    CHECK(t.knob_record(32, 0).empty() && t.knob_record(0, 32).empty() && t.name_record(-1).empty());
}

// edits change the records the way the original's value handler does (0x18008b020): names padded with spaces and cut
// to 16 / 10 characters, MIDI Channel Behaviour = bit 0 of the flags byte, Knob Behaviour in two nibbles
TEST(remote_templates_edit)
{
    RemoteTemplates t;
    t.set_name(2, "Ableton Live Device Ctrl");
    CHECK(t.name(2) == "Ableton Live Dev");
    CHECK(std::string(t.name_record(2).begin() + 9, t.name_record(2).begin() + 25) == "Ableton Live Dev");
    t.set_name(2, "Live");
    CHECK(std::string(t.name_record(2).begin() + 9, t.name_record(2).begin() + 25) == "Live            ");
    t.set_channel_enabled(2, true);
    CHECK(t.channel_enabled(2) && t.name_record(2)[25] == 0x01);
    t.set_channel_enabled(2, false);
    CHECK(!t.channel_enabled(2) && t.name_record(2)[25] == 0x00);

    t.set_knob_name(2, 5, "Cutoff Frequency");
    CHECK(t.knob_name(2, 5) == "Cutoff Fre");
    CHECK(std::string(t.knob_record(2, 5).begin() + 9, t.knob_record(2, 5).begin() + 19) == "Cutoff Fre");
    t.set_knob_behaviour(2, 5, 0x5A);
    CHECK(t.knob_behaviour(2, 5) == 0x5A && t.knob_record(2, 5)[19] == 0x0A && t.knob_record(2, 5)[20] == 0x05);
    CHECK(t.knob_name(2, 4).empty() && t.name(3) == "Init Template"); // nothing else changed
    t.set_name(40, "x");                                             // out of range: ignored
    t.set_knob_name(0, 40, "x");
    CHECK(t.name(0) == "Init Template");
}

// records from a cache or a template file: knob records (65 bytes), name records (28), and the older 27-byte name
// record without the flags byte, which the original converts (0x18008b8d0)
TEST(remote_templates_take_records)
{
    RemoteTemplates src;
    src.set_knob_name(4, 9, "Reso");
    src.set_knob_behaviour(4, 9, 3);
    RemoteTemplates t;
    CHECK(t.take_record(src.knob_record(4, 9)));
    CHECK(t.knob_name(4, 9) == "Reso" && t.knob_behaviour(4, 9) == 3);
    std::vector<uint8_t> bad = src.knob_record(4, 9);
    bad[8] = 40; // knob out of range
    CHECK(!t.take_record(bad));
    bad = src.knob_record(4, 9);
    bad[5] = 0x00; // another device id: the original checks 01 10
    CHECK(!t.take_record(bad));

    const uint8_t old[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13, 0x06, 0x20, 'O', 'l', 'd', ' ', ' ', ' ', ' ', ' ',
                           ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 0xF7};
    CHECK(t.take_record(std::vector<uint8_t>(old, old + sizeof old)));
    const auto &n = t.name_record(6);
    CHECK(n.size() == 28 && t.name(6) == "Old" && n[25] == 0 && n[26] == 0x00 && n[27] == 0xF7);
}

// the cache file round trip ({common_appdata}/Access Music/Virus TI/Cache/RemoteSets in the original): 32 x (name
// record + 32 knob records), the same layout as the original's (checked on a real one: 71688 bytes)
TEST(remote_templates_cache)
{
    RemoteTemplates a;
    a.set_name(31, "Last");
    a.set_channel_enabled(31, true);
    a.set_knob_name(0, 0, "First");
    auto bytes = a.cache();
    CHECK(bytes.size() == 71688);
    CHECK(bytes[0] == 'M' && bytes[3] == 'I' && bytes[4] == 0x00 && bytes[5] == 0x01 && bytes[6] == 0x18 && bytes[7] == 0);
    CHECK(bytes[8] == 0 && bytes[11] == 28 && bytes[12] == 0xF0); // template 0's name record first
    CHECK(bytes[8 + 4 + 28 + 3] == 65);                        // then its knob records

    RemoteTemplates b;
    CHECK(b.load_cache(bytes) == 32 * 33);
    CHECK(b.name(31) == "Last" && b.channel_enabled(31) && b.knob_name(0, 0) == "First");
    CHECK(b.cache() == bytes);

    RemoteTemplates c;
    CHECK(c.load_cache(container({a.name_record(31)})) == 1 && c.name(31) == "Last"); // a part is fine
    CHECK(c.load_cache({'R', 'I', 'F', 'F', 0, 0, 0, 0}) == 0);                     // not a MIDI container
    std::vector<uint8_t> cut = bytes;
    cut.resize(100); // a truncated file: the complete records before the cut
    RemoteTemplates d;
    CHECK(d.load_cache(cut) == 1);
}
