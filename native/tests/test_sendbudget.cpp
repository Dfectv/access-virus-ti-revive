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
