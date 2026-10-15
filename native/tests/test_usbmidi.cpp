// USB-MIDI packing (plug-in -> Virus) and parsing (Virus -> plug-in)
#include "check.h"
#include "device/usbmidi.h"

using namespace vti;
using Bytes = std::vector<uint8_t>;

static std::vector<std::pair<int, Bytes>> parse(const Bytes &packets, size_t chunk = 1 << 20)
{
    std::vector<std::pair<int, Bytes>> out;
    UsbMidiParser p([&](uint8_t, int cable, const Bytes &m) { out.push_back({cable, m}); });
    for (size_t i = 0; i < packets.size(); i += chunk) // USB transfers can end anywhere on a packet boundary
        p.feed(0x86, packets.data() + i, std::min(chunk, packets.size() - i));
    return out;
}

TEST(usbmidi_channel_messages)
{
    Bytes on = {0x91, 60, 100};
    auto pk = usbmidi_pack(on, 2);
    CHECK_EQ(pk.size(), 4u);
    CHECK_EQ(pk[0], 0x29); // cable 2, code index 9 (note on)
    auto m = parse(pk);
    CHECK_EQ(m.size(), 1u);
    CHECK(m[0].first == 2 && m[0].second == on);
    Bytes pressure = {0xD3, 77}; // 2-byte channel message
    m = parse(usbmidi_pack(pressure, 2));
    CHECK(m.size() == 1 && m[0].second == pressure);
}

TEST(usbmidi_system_messages)
{
    for (Bytes msg : {Bytes{0xF8}, Bytes{0xFE}, Bytes{0xF2, 0x10, 0x01}, Bytes{0xF5, 0x7F, 0x7F}}) {
        auto pk = usbmidi_pack(msg, 5);
        CHECK_EQ(pk.size(), 4u);
        auto m = parse(pk);
        CHECK(m.size() == 1 && m[0].first == 5 && m[0].second == msg);
    }
}

TEST(usbmidi_sysex_every_length)
{
    for (size_t n = 2; n <= 40; n++) { // F0 .. F7, every remainder of the 3-byte packets
        Bytes sx(n, 0x11);
        sx.front() = 0xF0, sx.back() = 0xF7;
        auto pk = usbmidi_pack(sx, 2);
        CHECK_EQ(pk.size(), (n + 2) / 3 * 4);
        for (size_t chunk : {4u, 8u, 64u}) {
            auto m = parse(pk, chunk);
            CHECK(m.size() == 1 && m[0].second == sx);
        }
    }
}

TEST(usbmidi_interleaved_cables)
{
    Bytes a = {0xF0, 1, 2, 3, 4, 5, 0xF7}, b = {0xF0, 9, 8, 7, 6, 0xF7};
    Bytes pa = usbmidi_pack(a, 2), pb = usbmidi_pack(b, 3), mixed;
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); i += 4) { // packet by packet, alternating cables
        if (i < pa.size())
            mixed.insert(mixed.end(), pa.begin() + i, pa.begin() + i + 4);
        if (i < pb.size())
            mixed.insert(mixed.end(), pb.begin() + i, pb.begin() + i + 4);
    }
    auto m = parse(mixed);
    CHECK_EQ(m.size(), 2u);
    bool got_a = false, got_b = false;
    for (auto &x : m)
        got_a |= x.first == 2 && x.second == a, got_b |= x.first == 3 && x.second == b;
    CHECK(got_a && got_b);
}

TEST(usbmidi_empty_packets_ignored)
{
    Bytes pk(16, 0); // zero padding as some hosts/controllers send it
    CHECK(parse(pk).empty());
    CHECK(usbmidi_pack({}, 2).empty());
}
