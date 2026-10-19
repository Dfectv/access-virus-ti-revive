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
