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
    // note on C4 velocity 100 on channel 1, cable 2; Active Sensing (CIN F)
    auto n = run(d, payload(0, {0x29, 0x90, 60, 100, 0x2F, 0xFE, 0, 0}));
    CHECK(n.size() == 2 && n[0].text == "c2 Note on  ch1 C4 vel 100" && n[1].text == "c2 Active Sensing");
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
