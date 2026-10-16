// pktstat.h - sizes of the USB audio IN packets the Virus sends, for E175 (Linux: after a replug / power-on the
// stream keeps running but its packets leave the 44.1 kHz range ~2.8 s after every start; the code only counted the
// good ones, so the bad sizes were never seen). Diagnostics only: counting, no request to the Virus. Plain C, used by
// the libusb (Linux) and the WinUSB (Windows) stream; the unit test calls it directly.
#pragma once
#include <stdint.h>

enum { VTI_PKT_EMPTY, VTI_PKT_SHORT, VTI_PKT_43, VTI_PKT_44, VTI_PKT_45, VTI_PKT_46, VTI_PKT_48K, VTI_PKT_LONG,
       VTI_PKT_BUCKETS };
#define VTI_PKT_BAD_RUN 1000 // 1 s of out-of-range packets: longer than the 48 kHz phase right after SET_CUR (~0.3 s)
#define VTI_PKT_GOOD_RUN 1000

typedef struct {
    uint64_t size[VTI_PKT_BUCKETS]; // packets per size bucket, frames: 0, 1-42, 43, 44, 45, 46, 47-49, more
    uint64_t packets;
    uint32_t bad_run, good_run;     // current run lengths
    int bad;                        // 1 while in a reported bad run
    uint64_t bad_since;             // packet number (= ms since the start) of the first packet of that run
    uint32_t bad_frames;            // size of the run's last bad packet, in frames
} vti_pktstat;

static inline int vti_pktstat_bucket(uint32_t f)
{
    return f == 0 ? VTI_PKT_EMPTY : f < 43 ? VTI_PKT_SHORT : f <= 46 ? VTI_PKT_43 + (int)(f - 43)
           : f <= 49 ? VTI_PKT_48K : VTI_PKT_LONG;
}

// one packet of f frames: +1 = a bad run just reached VTI_PKT_BAD_RUN, -1 = it ended (VTI_PKT_GOOD_RUN good ones)
static inline int vti_pktstat_add(vti_pktstat *s, uint32_t f)
{
    int ok = f >= 43 && f <= 46, ev = 0;
    s->size[vti_pktstat_bucket(f)]++;
    if (ok) {
        s->good_run++;
        if (s->bad && s->good_run >= VTI_PKT_GOOD_RUN) {
            s->bad = 0;
            ev = -1;
        }
        if (s->good_run >= VTI_PKT_GOOD_RUN || !s->bad)
            s->bad_run = 0;
    } else {
        s->good_run = 0;
        if (s->bad_run++ == 0 && !s->bad)
            s->bad_since = s->packets;
        s->bad_frames = f;
        if (!s->bad && s->bad_run >= VTI_PKT_BAD_RUN) {
            s->bad = 1;
            ev = 1;
        }
    }
    s->packets++;
    return ev;
}
