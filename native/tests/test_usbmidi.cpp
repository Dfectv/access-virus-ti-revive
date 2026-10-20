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

// Mutation run 2026-10-20: the lowest status byte and the unused bytes of a packet were not pinned down.
TEST(usbmidi_note_off_is_a_channel_message)
{
    auto pk = usbmidi_pack({0x80, 60, 0}, 2);
    CHECK(pk.size() == 4 && pk[0] == 0x28); // code index 8, not a system message
}

// A packet's unused bytes are 0; packing must not read past the message (here: spare capacity holding 0x55)
TEST(usbmidi_pack_pads_with_zero_not_with_bytes_past_the_message)
{
    for (Bytes msg : {Bytes{0xF0, 0x11, 0x22, 0xF7}, Bytes{0xF0, 0x11, 0x22, 0x33, 0xF7}, Bytes{0xD3}, Bytes{0xD3, 77},
                      Bytes{0xF8}, Bytes{0xF3, 0x05}}) {
        Bytes v = msg;
        const size_t n = v.size();
        v.insert(v.end(), {0x55, 0x55, 0x55});
        v.resize(n);
        for (uint8_t b : usbmidi_pack(v, 2))
            CHECK(b != 0x55);
    }
}

// MIDI lets real-time bytes appear inside a SysEx (USB-MIDI: code index F on the same cable); they are their own
// message and leave the SysEx whole. A single-byte system common message (code index 5) outside a SysEx is delivered.
TEST(usbmidi_real_time_inside_sysex_and_single_byte_common)
{
    Bytes pk = {0x24, 0xF0, 0x00, 0x20, 0x2F, 0xF8, 0, 0, 0x26, 0x33, 0xF7, 0, 0x25, 0xF6, 0, 0};
    auto m = parse(pk);
    CHECK_EQ(m.size(), 3u);
    CHECK(m.size() == 3 && m[0].second == Bytes{0xF8} && m[1].second == (Bytes{0xF0, 0x00, 0x20, 0x33, 0xF7}) &&
          m[2].second == Bytes{0xF6});
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

// A SysEx cut off in the middle (the broker went away, the Virus switched off: review 2026-10-19) left its bytes in
// the parser, and the next SysEx on that cable was glued onto them - the readiness probe's reply after a broker
// reconnect was never recognised (11.5 s "no reply to the probe", TI Desktop, hardware test). A new F0 starts a new
// message; the cut-off part is dropped.
TEST(usbmidi_cut_off_sysex_does_not_glue_onto_the_next)
{
    Bytes cut = usbmidi_pack({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0xF7}, 3);
    cut.resize(8); // only the first two packets (6 bytes, no end) arrived
    Bytes next = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7};
    Bytes all = cut;
    auto pk = usbmidi_pack(next, 3);
    all.insert(all.end(), pk.begin(), pk.end());
    auto out = parse(all);
    CHECK(out.size() == 1);
    CHECK(!out.empty() && out[0].second == next);
}

// Review 2026-10-19 (M9): a stream of SysEx packets that never ends must not grow without bound.
TEST(usbmidi_endless_sysex_is_bounded)
{
    UsbMidiParser p([](uint8_t, int, const Bytes &) {});
    Bytes start = {0x34, 0xF0, 0x00, 0x20}, more = {0x34, 0x01, 0x02, 0x03};
    p.feed(0x86, start.data(), start.size());
    for (int i = 0; i < 100000; i++) // 300 kB without F7
        p.feed(0x86, more.data(), more.size());
    CHECK(p.pending_bytes() <= UsbMidiParser::MAX_SYSEX);
}
