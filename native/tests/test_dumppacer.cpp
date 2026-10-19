// A single dump on cable 3 goes out in pieces with the notes in between (dumppacer.h, round 0.8.27 L3): the
// original writes 64 bytes every 6-12 ms and sends its notes and clock ticks on cable 5 meanwhile (DSPs only), so
// the Virus' host CPU parses the dump undisturbed and the other parts keep playing (capture, E270).
#include "check.h"
#include "device/dumppacer.h"

#include <vector>

using namespace vti;

static const int64_t MS = 1000000;

TEST(dumppacer_splits_a_dump_into_paced_pieces)
{
    DumpPacer p;
    CHECK(!p.active());
    std::vector<uint8_t> pk(700, 0x34); // 175 USB-MIDI packets of a 524-byte dump
    p.start(pk, 0);
    CHECK(p.active());
    std::vector<uint8_t> out;
    CHECK(p.next(0, out) && out.size() == 64);  // the first piece at once
    CHECK(!p.next(9 * MS, out));                 // not before 10 ms
    CHECK(p.next(10 * MS, out) && out.size() == 64);
    int pieces = 2;
    for (int64_t t = 20 * MS; p.active(); t += 10 * MS)
        if (p.next(t, out))
            pieces++;
    CHECK(pieces == 11);        // 10 x 64 + 60
    CHECK(out.size() == 60);    // the last piece holds the rest
    CHECK(!p.active());
}

TEST(dumppacer_events_go_on_cable_5_while_a_dump_runs)
{
    DumpPacer p;
    CHECK(p.event_cable(2) == 2);
    std::vector<uint8_t> pk(128, 0x34);
    p.start(pk, 0);
    CHECK(p.event_cable(2) == 5); // notes, clock, Active Sensing: to the DSPs only, the host CPU is in the dump
    std::vector<uint8_t> out;
    p.next(0, out);
    CHECK(p.event_cable(2) == 5);
    p.next(10 * MS, out);
    CHECK(!p.active());
    CHECK(p.event_cable(2) == 2);
    CHECK(p.wait_ns(10 * MS) == 0);
}

TEST(dumppacer_says_how_long_to_wait)
{
    DumpPacer p;
    std::vector<uint8_t> pk(256, 0x34), out;
    p.start(pk, 0);
    p.next(0, out);
    CHECK(p.wait_ns(2 * MS) == 8 * MS);
    CHECK(p.wait_ns(11 * MS) == 0);
}

TEST(dumppacer_holds_parameters_until_the_dump_is_out)
{
    // automation (parameter SysEx from process()) that falls into a dump would go on cable 5 with the notes and
    // reach only the DSPs - the host CPU would not store the value (E274); it waits for the dump's end instead,
    // the latest value per parameter
    HeldParams h;
    const std::vector<uint8_t> cut1 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x10, 0xF7};
    const std::vector<uint8_t> cut2 = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x20, 0xF7};
    const std::vector<uint8_t> reso = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x29, 0x30, 0xF7};
    const std::vector<uint8_t> other_part = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x01, 0x28, 0x40, 0xF7};
    CHECK(HeldParams::held(cut1.data(), cut1.size()));
    const uint8_t note[] = {0x90, 60, 100};
    CHECK(!HeldParams::held(note, 3)); // notes go between the pieces
    h.add(cut1);
    h.add(reso);
    h.add(cut2); // replaces cut1
    h.add(other_part);
    auto out = h.take();
    CHECK(out.size() == 3);
    CHECK(out[0] == cut2 && out[1] == reso && out[2] == other_part); // in first-arrival order, latest value
    CHECK(h.take().empty());
}
