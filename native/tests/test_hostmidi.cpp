// What the host hands process() becomes MIDI bytes for the Virus (hostmidi.h, review 2026-10-19): a value outside the
// VST3 range (velocity 1.5, a negative aftertouch, pitch 200) gave a byte >= 0x80, which the Virus reads as a status
// byte (0xF4: its DSP's private command parser, rule 2); a tempo of 0 or below made the MIDI clock loop endless on
// the audio thread.
#include "check.h"
#include "device/hostmidi.h"
#include <cmath>
#include <limits>

using namespace vti;

TEST(hostmidi_data_bytes_stay_below_0x80)
{
    CHECK(data7(0.0) == 0 && data7(1.0) == 127 && data7(0.5) == 64);
    CHECK(data7(1.5) == 127);
    CHECK(data7(-0.3) == 0);
    CHECK(data7(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(note7(60) == 60 && note7(200) == 127 && note7(-5) == 0);
}

TEST(hostmidi_pitch_bend_stays_14_bit)
{
    CHECK(bend14(0.5) == 8192);
    CHECK(bend14(1.0) == 16383 && bend14(2.0) == 16383);
    CHECK(bend14(-1.0) == 0);
    CHECK((bend14(7.0) >> 7) < 0x80);
}

TEST(hostmidi_clock_tempo_is_sane)
{
    CHECK(clock_tempo(120.0) == 120.0);
    CHECK(clock_tempo(0.0) == 120.0);   // no tempo: the default, like without a process context
    CHECK(clock_tempo(-60.0) == 120.0);
    CHECK(clock_tempo(std::numeric_limits<double>::quiet_NaN()) == 120.0);
    CHECK(clock_tempo(5000.0) == 999.0); // at most ~5 ticks per 512-sample block
    CHECK(clock_tempo(0.5) == 1.0);
}
