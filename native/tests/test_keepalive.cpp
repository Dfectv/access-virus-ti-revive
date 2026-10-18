// Active Sensing (FE) on the stamped timeline: one FE every 300 ms like the original Virus Control (E025, median
// 0.31 s). The Virus' DSP arms a watchdog with the first FE it sees and releases every voice about one second
// after the last one (E237) - so FE goes out either never or regularly, never just once.
#include "check.h"
#include "device/keepalive.h"
#include <vector>

using namespace vti;

TEST(keepalive_period_on_timeline)
{
    ActiveSensing fe;
    std::vector<int64_t> at;
    auto emit = [&](int64_t t) { at.push_back(t); };
    CHECK(fe.run(100000, emit) == 0); // not anchored: nothing
    fe.reset(1000 + fe.period);       // the anchor sent its own FE at 1000; the next one is due a period later
    for (int64_t end = 1512; end <= 1000 + 44100 * 10; end += 512)
        fe.run(end, emit);
    CHECK(at.size() == 33); // 10 s at 300 ms, the first at 1000 + 13230
    bool exact = !at.empty() && at[0] == 1000 + 13230;
    for (size_t i = 1; i < at.size(); i++)
        exact = exact && at[i] - at[i - 1] == 13230;
    CHECK(exact);
}

TEST(keepalive_resync_bounds_backlog_and_future)
{
    ActiveSensing fe;
    std::vector<int64_t> at;
    auto emit = [&](int64_t t) { at.push_back(t); };
    fe.reset(0);
    // the timeline jumped forward by 5 s (host stall): at most one FE per block, no burst of 16
    CHECK(fe.run(44100 * 5, emit) == 1);
    CHECK(at.size() == 1 && at[0] >= 44100 * 5 - 13230);
    // the timeline jumped back by 5 s: the next FE is still due within one period, not 5 s later
    at.clear();
    int64_t n = 0;
    for (int64_t end = 512; end <= 13230 + 512; end += 512)
        n += fe.run(end, emit);
    CHECK(n == 1);
}
