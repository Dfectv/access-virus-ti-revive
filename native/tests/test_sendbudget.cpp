// The byte budget for stamped MIDI traffic (sendbudget.h): musical rates pass untouched, a flood is spread out so
// that no 160 ms window carries more than the DSP's 2046-byte stamp queue holds (E238).
#include "check.h"
#include "device/sendbudget.h"

using namespace vti;

static const int64_t BLOCK_NS = 11609977; // 512 frames at 44.1 kHz

TEST(sendbudget_musical_rates_pass)
{
    SendBudget b; // 4000 B/s, burst 1000
    int64_t now = 0;
    bool ok = true;
    for (int i = 0; i < 86 * 10; i++, now += BLOCK_NS) { // 10 s of automation at 250/s (6 bytes each) + 20 notes/s
        int bytes = 3 * 6 + (i % 4 == 0 ? 8 : 0);
        ok = ok && b.available(now) >= bytes;
        b.take(bytes);
    }
    CHECK(ok);
}

TEST(sendbudget_flood_is_spread)
{
    SendBudget b;
    int64_t now = 0;
    // a 3600-byte burst (the old hang test's 160 ms worth) arrives at once: at most the burst goes now
    int first = b.available(now);
    CHECK(first == 1000);
    b.take(first);
    int sent = first, window = first;
    int64_t t = now;
    // the rest trickles at the sustained rate: within any 160 ms window less than 2046 bytes
    bool bounded = true;
    for (int i = 1; i <= 86 * 2 && sent < 3600; i++) { // up to 2 s
        t = now + i * BLOCK_NS;
        int n = b.available(t);
        if (n > 3600 - sent)
            n = 3600 - sent;
        b.take(n);
        sent += n;
        if (i <= 14) // the first 160 ms (14 blocks)
            window += n;
    }
    bounded = window < 2046 && sent == 3600;
    CHECK(bounded);
}

TEST(sendbudget_idle_does_not_accumulate)
{
    SendBudget b;
    b.available(0);
    CHECK(b.available(60LL * 1000000000LL) == 1000); // a minute idle: still only one burst
    b.rate = 0;                                       // unlimited (VTI_NATIVE_MIDI_RATE=0)
    CHECK(b.available(1) >= 100000);
}

// Round 0.8.27 (T2): queued SysEx on cable 2 (part dumps, parameter changes from the editor, the state) sits in
// the same DSP queue once it follows a stamp that is beyond the threshold (E261), so it counts against the budget
// too - but as a debt capped at one burst: a 100 kB state send must not silence the notes for 25 s afterwards.
TEST(sendbudget_queued_bytes_are_a_bounded_debt)
{
    SendBudget b;
    b.available(0);
    b.take_queued(520); // one part dump
    CHECK(b.available(0) == 480);
    b.take_queued(520); // a second one: the stamped burst right after both is gone
    CHECK(b.available(0) == 0);
    b.take_queued(100000); // a whole state send
    CHECK(b.available(0) == 0);
    SendBudget c; // the debt is at most one burst: 250 ms later it is paid, 500 ms later the burst is back

    c.available(0);
    c.take_queued(100000);
    CHECK(c.available(250000000LL) == 0);  // 1000 bytes of debt, 1000 earned
    CHECK(c.available(500000000LL) == 1000); // and the burst is back
    SendBudget d;
    d.rate = 0; // unlimited (tests): no debt either
    d.take_queued(100000);
    CHECK(d.available(1) >= 100000);
}
