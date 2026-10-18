// The USB audio IN packet statistics (E175): sizes of the packets the Virus sends, and when the stream leaves and
// re-enters the 44.1 kHz range for longer than the short 48 kHz phase right after SET_CUR. Hardware-free.
#include "check.h"
extern "C" {
#include "pktstat.h"
}

TEST(pktstat_buckets)
{
    vti_pktstat s = {};
    const uint32_t sizes[] = {0, 12, 43, 44, 44, 45, 46, 48, 49, 64};
    for (uint32_t f : sizes)
        vti_pktstat_add(&s, f);
    CHECK(s.size[VTI_PKT_EMPTY] == 1);
    CHECK(s.size[VTI_PKT_SHORT] == 1);
    CHECK(s.size[VTI_PKT_43] == 1);
    CHECK(s.size[VTI_PKT_44] == 2);
    CHECK(s.size[VTI_PKT_45] == 1);
    CHECK(s.size[VTI_PKT_46] == 1);
    CHECK(s.size[VTI_PKT_48K] == 2);
    CHECK(s.size[VTI_PKT_LONG] == 1);
}

TEST(pktstat_bad_run)
{
    vti_pktstat s = {};
    // the 48 kHz phase after SET_CUR (~0.3 s) is no event
    for (int i = 0; i < 300; i++)
        CHECK(vti_pktstat_add(&s, 48) == 0);
    for (int i = 0; i < 2000; i++)
        CHECK(vti_pktstat_add(&s, 44) == 0);
    CHECK(s.packets == 2300);
    // a long run of out-of-range packets is reported once, when it reaches VTI_PKT_BAD_RUN packets
    int events = 0;
    for (int i = 0; i < VTI_PKT_BAD_RUN - 1; i++)
        events += vti_pktstat_add(&s, 0) != 0;
    CHECK(events == 0 && !s.bad);
    CHECK(vti_pktstat_add(&s, 0) == 1);
    CHECK(s.bad && s.bad_since == 2300 && s.bad_frames == 0);
    for (int i = 0; i < 5000; i++)
        CHECK(vti_pktstat_add(&s, 0) == 0);
    // one good packet inside a bad run is not the end of it, a run of good ones is
    CHECK(vti_pktstat_add(&s, 44) == 0);
    CHECK(vti_pktstat_add(&s, 0) == 0);
    int end = 0;
    for (int i = 0; i < VTI_PKT_GOOD_RUN; i++)
        end += vti_pktstat_add(&s, 44 + (i % 10 == 0)) == -1;
    CHECK(end == 1 && !s.bad);
}

// E241: after a wake the Virus kept sending 48 kHz packets while the broker mirrored 44/45-frame OUT packets with
// audio at it; a loud signal on its outputs, then a freeze. The OUT packet now matches a 48 kHz packet's size and is
// silent; a lost or empty packet keeps the old rule (nominal size, audio).
TEST(pktstat_out_matches_48k_with_silence)
{
    int silent = -1;
    CHECK(vti_pkt_out(44, 44, &silent) == 44 && silent == 0);
    CHECK(vti_pkt_out(46, 45, &silent) == 46 && silent == 0);
    CHECK(vti_pkt_out(48, 44, &silent) == 48 && silent == 1);
    CHECK(vti_pkt_out(47, 45, &silent) == 47 && silent == 1);
    CHECK(vti_pkt_out(49, 44, &silent) == 49 && silent == 1);
    CHECK(vti_pkt_out(0, 45, &silent) == 45 && silent == 0);  // lost packet: as before
    CHECK(vti_pkt_out(60, 44, &silent) == 44 && silent == 0); // nonsense size: as before
}

// the rate is lost when a reported bad run consists of 48 kHz packets (not when packets are merely lost)
TEST(pktstat_rate_lost_only_for_48k_runs)
{
    vti_pktstat s = {};
    for (int i = 0; i < VTI_PKT_BAD_RUN; i++)
        vti_pktstat_add(&s, 48);
    CHECK(s.bad && vti_pktstat_rate_lost(&s));
    vti_pktstat t = {};
    for (int i = 0; i < VTI_PKT_BAD_RUN; i++)
        vti_pktstat_add(&t, 0);
    CHECK(t.bad && !vti_pktstat_rate_lost(&t));
    vti_pktstat u = {};
    for (int i = 0; i < 300; i++) // the short 48 kHz phase right after SET_CUR is not "lost"
        vti_pktstat_add(&u, 48);
    CHECK(!vti_pktstat_rate_lost(&u));
}
