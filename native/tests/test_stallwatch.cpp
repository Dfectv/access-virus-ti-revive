// A Virus audio stream that stood still while time went on (macOS sleep: Core Audio pauses, the ring keeps its frame
// counter and runs on after wake, E207 M3): the plug-in must treat it as a new stream and re-anchor, else every
// note is stamped by the length of the pause off and nothing sounds.
#include "check.h"
#include "device/stallwatch.h"

using namespace vti;

TEST(stallwatch_steady_stream_is_no_stall)
{
    StallWatch w;
    int64_t frame = 1000;
    for (int64_t t = 0; t < 10000; t += 10, frame += 441)
        CHECK(!w.update(frame, t));
}

TEST(stallwatch_short_gaps_are_no_stall)
{
    // the worker polls faster than USB packets arrive, and a busy system can hold the ring for a few hundred ms
    StallWatch w;
    w.update(1000, 0);
    CHECK(!w.update(1000, 400)); // standing still is not reported by itself
    CHECK(!w.update(5000, 500)); // moved on after 0.5 s: a hiccup, the timeline follows it
}

TEST(stallwatch_resume_after_sleep_is_reported_once)
{
    StallWatch w;
    w.update(969472, 0);
    CHECK(!w.update(969472, 19000)); // asleep: no movement, nothing yet
    CHECK(w.update(970000, 19050));  // awake: the stream runs on - a new stream for the timeline
    CHECK(!w.update(970441, 19060)); // once
}

TEST(stallwatch_worker_paused_too)
{
    // the whole process slept (the worker did not run): the first call after wake sees the jump at once
    StallWatch w;
    w.update(1000, 0);
    CHECK(w.update(1441, 30000));
}

TEST(stallwatch_no_stream_resets)
{
    StallWatch w;
    w.update(1000, 0);
    CHECK(!w.update(-1, 5000));   // no audio (Virus gone): the reconnect path handles it
    CHECK(!w.update(2000, 9000)); // the new stream's first frame is no stall
}
