// Nerd mode's traffic decoder (src/cc/decode) on hand-made USB-MIDI packets: SysEx put back together across packets,
// documented Access commands named, channel messages; without the device model (CI has no Access files).
#include "cc/decode.h"
#include "check.h"
#include <cstring>

using namespace vti::cc;

namespace {
// an OBS_TRAFFIC payload: kind, ep, ms, packets
std::vector<uint8_t> payload(int kind, std::vector<uint8_t> packets)
{
    std::vector<uint8_t> p = {(uint8_t)kind, (uint8_t)(kind ? 0x86 : 0x06), 0x10, 0x27, 0, 0}; // 10000 ms
    p.insert(p.end(), packets.begin(), packets.end());
    return p;
}
std::vector<TrafficLine> run(TrafficDecoder &d, const std::vector<uint8_t> &p)
{
    std::vector<TrafficLine> out;
    d.feed(p.data(), p.size(), [&](TrafficLine &&l) { out.push_back(std::move(l)); });
    return out;
}
} // namespace

TEST(decode_sysex_and_notes)
{
    TrafficDecoder d;
    // single dump request RAM B #13 on cable 2: F0 00 20 33 01 10 30 02 0C F7 (CIN 4,4,4 then 5 with F7)
    auto l = run(d, payload(0, {0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10, 0x24, 0x30, 0x02, 0x0C, 0x25, 0xF7, 0, 0}));
    CHECK(l.size() == 1);
    CHECK(l.size() == 1 && l[0].text == "c2 Single dump request RAM B #13" && l[0].dir == 0 && l[0].ms == 10000);
    // the same split over two observer messages (the decoder keeps the partial SysEx per direction and cable)
    auto a = run(d, payload(1, {0x34, 0xF0, 0x00, 0x20, 0x34, 0x33, 0x01, 0x10}));
    auto b = run(d, payload(1, {0x37, 0x39, 0x01, 0xF7}));
    CHECK(a.empty() && b.size() == 1 && b[0].text == "c3 Bank info request RAM A");
    // the complete message comes along (virusmon --hex)
    CHECK(l.size() == 1 && l[0].bytes == (std::vector<uint8_t>{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x02, 0x0C, 0xF7}));
    CHECK(b.size() == 1 && b[0].bytes.size() == 9 && b[0].bytes.back() == 0xF7); // F0 00 20 33 01 10 39 01 F7
    // note on C4 velocity 100 on channel 1, cable 2; Active Sensing (CIN F)
    auto n = run(d, payload(0, {0x29, 0x90, 60, 100, 0x2F, 0xFE, 0, 0}));
    CHECK(n.size() == 2 && n[0].text == "c2 Note on  ch1 C4 vel 100" && n[1].text == "c2 Active Sensing");
    CHECK(n.size() == 2 && n[0].bytes == (std::vector<uint8_t>{0x90, 60, 100}) && n[1].bytes.size() == 1);
    // parameter page without a model: numbers
    std::string kind;
    CHECK(d.describe({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x00, 0x21, 0x05, 0xF7}, kind) ==
              "Parameter page 72 part 00 id 21 = 5" && kind == "param");
    // control request record: setup (IN, GET_DESCRIPTOR device), result 18
    std::vector<uint8_t> c = {2, 0, 0, 0, 0, 0, 0x80, 6, 0, 1, 0, 0, 18, 0, 18, 0, 0, 0};
    auto cl = run(d, c);
    CHECK(cl.size() == 1 && cl[0].dir == 2 &&
          cl[0].text == "USB control IN req 06 value 0100 index 0000 len 18 -> 18");
}

TEST(decode_describe_sysex_kinds)
{
    TrafficDecoder d;
    std::string kind;
    auto say = [&](const std::vector<uint8_t> &m) { return d.describe(m, kind); };
    CHECK(say({}).empty());
    CHECK(say({0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}) == "Identity Request" && kind == "sysex");
    std::vector<uint8_t> other = {0xF0, 0x41};
    for (int i = 0; i < 27; i++)
        other.push_back((uint8_t)i);
    other.push_back(0xF7); // 30 bytes: hex shows the first 24
    std::string s = say(other);
    CHECK(s.rfind("SysEx (30 bytes): F0 41 00 01", 0) == 0 && s.size() > 4 && s.substr(s.size() - 4) == " ...");
    std::vector<uint8_t> single(524, 0);
    const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x02, 0x04};
    std::copy(head, head + 9, single.begin());
    const char name[] = "Lead Synth";
    std::copy(name, name + 10, single.begin() + 249);
    CHECK(say(single) == "Single dump RAM B #5 \"Lead Synth\"" && kind == "dump");
    CHECK(say({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x02, 0xF7}) == "Single dump edit buffer part 3 (10 bytes)");
    CHECK(say({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x11, 0x00, 0x00, 0xF7}) == "Multi dump (10 bytes)" && kind == "dump");
    CHECK(say({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x09, 0x00, 0xF7}) == "Bank info bank 9 (fingerprints)");
    CHECK(say({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}) == "Sync ping (global 0x22)" &&
          kind == "param");
    CHECK(say({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x50, 0x01, 0xF7}) ==
          "Access SysEx cmd 50 (9 bytes): F0 00 20 33 01 10 50 01 F7");
}

TEST(decode_describe_channel_and_system)
{
    TrafficDecoder d;
    std::string kind;
    auto say = [&](const std::vector<uint8_t> &m) { return d.describe(m, kind); };
    CHECK(say({0xF8}) == "Clock" && kind == "clock");
    CHECK(say({0xFA}) == "Start" && say({0xFC}) == "Stop" && say({0xFB}) == "Continue");
    CHECK(say({0xF2, 0x10, 0x01}) == "Song position 144");
    CHECK(say({0xF5, 0x7F, 0x01}) == "Timing F5 7F 01" && kind == "clock");
    CHECK(say({0x81, 61, 0}) == "Note off ch2 C#4 vel 0" && kind == "note");
    CHECK(say({0x90, 69, 0}) == "Note off ch1 A4 vel 0"); // note on with velocity 0 = note off
    CHECK(say({0xB0, 7, 100}) == "CC ch1 #7 = 100" && kind == "cc");
    CHECK(say({0xA2, 10, 5}) == "Poly pressure ch3 #10 = 5");
    CHECK(say({0xC0, 4}) == "Program change ch1 5" && kind == "cc");
    CHECK(say({0xE0, 0x00, 0x40}) == "Pitch bend ch1 0");
    CHECK(say({0xEF, 0x7F, 0x7F}) == "Pitch bend ch16 8191");
    CHECK(say({0xF6}) == "F6" && kind == "other");      // tune request: shown as hex
    CHECK(say({0xB0, 7}) == "B0 07" && kind == "other"); // truncated CC
    CHECK(say({0xD0, 0x40}) == "D0 40");                 // channel pressure: not named
}

TEST(decode_packet_kinds)
{
    TrafficDecoder d;
    // SysEx ending with 2 bytes (CIN 6), 2-byte messages (CIN C, D, 2), unknown CIN skipped
    auto l = run(d, payload(0, {0x14, 0xF0, 0x7E, 0x00, 0x16, 0x09, 0xF7, 0, 0x1C, 0xC0, 0x04, 0, 0x1D, 0xD0, 0x40, 0,
                                0x12, 0xF3, 0x05, 0, 0x10, 0x01, 0x02, 0x03}));
    CHECK_EQ(l.size(), 4u);
    CHECK(l.size() == 4 && l[0].text == "c1 SysEx (5 bytes): F0 7E 00 09 F7" && l[1].text == "c1 Program change ch1 5" &&
          l[2].text == "c1 D0 40" && l[3].text == "c1 F3 05");
    // a SysEx without an end (runaway) is shown once it passes 4 KB (at 3 + 1365 * 3 = 4098 bytes)
    std::vector<uint8_t> pk = {0x34, 0xF0, 0x01, 0x02};
    for (int i = 0; i < 1366; i++)
        pk.insert(pk.end(), {0x34, 0x01, 0x02, 0x03});
    l = run(d, payload(1, pk));
    CHECK(l.size() == 1 && l[0].text.rfind("c3 SysEx (4098 bytes): F0 01 02", 0) == 0 && l[0].dir == 1);
    // too short for a header, and a control record without its setup: nothing / an empty line
    CHECK(run(d, {0, 0x06, 0, 0}).empty());
    l = run(d, {2, 0, 0, 0, 0, 0, 0x80, 6});
    CHECK(l.size() == 1 && l[0].dir == 2 && l[0].kind == "control" && l[0].text.empty());
}
