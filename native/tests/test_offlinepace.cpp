// Offline render (export, freeze, offline bounce): the host calls process() faster than real time, but the Virus only
// makes sound in real time. The original refuses it (a warning, Virus Control 0x18002fd40 / 0x180080ba0); the native
// plug-in holds each offline block until the Virus has produced that much audio, so the render is right at 1x speed.
#include "check.h"
#include "device/offlinepace.h"

using namespace vti;

TEST(offlinepace_first_block_runs_at_once)
{
    OfflinePacer p;
    CHECK(p.wait_until(10000, 512) <= 10000); // nothing to wait for: the render starts where the Virus is now
}

TEST(offlinepace_holds_blocks_to_real_time)
{
    // the host asks for blocks back to back; each one needs the Virus' next 512 frames
    OfflinePacer p;
    p.wait_until(10000, 512);
    CHECK(p.wait_until(10000, 512) == 10512); // the Virus has not moved on yet: wait for 512 more frames
    CHECK(p.wait_until(10512, 512) == 11024);
    CHECK(p.wait_until(11024, 512) == 11536);
}

TEST(offlinepace_fractional_blocks_do_not_drift)
{
    // 48 kHz host: 512 host samples are 470.4 Virus frames; 10 s of blocks must ask for 441000 frames, not 440000
    OfflinePacer p;
    const double frames = 512 * 44100.0 / 48000.0;
    int64_t newest = 0, need = 0;
    p.wait_until(newest, frames);
    const int blocks = (int)(10 * 48000 / 512); // 937 blocks = 9.995 s
    for (int i = 0; i < blocks; i++) {
        need = p.wait_until(newest, frames);
        newest = need; // the Virus delivers exactly what was asked for
    }
    CHECK(need >= (int64_t)(blocks * frames) - 1 && need <= (int64_t)(blocks * frames) + 1);
}

TEST(offlinepace_slow_render_catches_up_without_a_burst)
{
    // a heavy project renders slower than real time: the Virus runs ahead; the paced position never falls further
    // behind than MAX_BEHIND, so the frames read there are still in the audio ring and no backlog builds up
    OfflinePacer p;
    p.wait_until(0, 512);
    CHECK(p.wait_until(200000, 512) == 200000 - OfflinePacer::MAX_BEHIND);
    CHECK(p.wait_until(200000, 512) == 200000 - OfflinePacer::MAX_BEHIND + 512);
    CHECK(OfflinePacer::MAX_BEHIND <= 16384 / 4); // lag + latency (~2-4k frames) + a block stay inside the ring
}

TEST(offlinepace_new_stream_restarts)
{
    // the Virus came back (a new audio stream starts low) or the wait gave up: start again where the Virus is
    OfflinePacer p;
    p.wait_until(500000, 512);
    p.wait_until(500000, 512);
    CHECK(p.wait_until(1000, 512) <= 1000); // the ring frame went backwards: a new stream, no wait
    p.reset();
    CHECK(p.wait_until(5000, 512) <= 5000); // after a reset (timeout, back to real time) the next block runs at once
}

TEST(offlinepace_no_audio_never_waits)
{
    OfflinePacer p;
    CHECK(p.wait_until(-1, 512) < 0); // no Virus audio: nothing to pace by
    CHECK(p.wait_until(-1, 512) < 0);
}
