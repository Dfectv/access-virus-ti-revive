// The broker's packetised device write with retries (src/broker/writeretry.h): a timed-out packet is sent again,
// the message stays intact, a real error stops at once, a partial acceptance (WinUSB) continues from there.
#include "check.h"
#include "writeretry.h"
#include <vector>

namespace {
struct Fake {
    std::vector<int> script; // per call: 0 = accept, 1 = timeout, -1 = error, 2 = accept half then timeout
    size_t call = 0;
    std::vector<uint8_t> got; // bytes accepted, in order
    std::vector<int> sizes;   // packet sizes offered
};
int xfer(void *ctx, const uint8_t *data, int n, int *done)
{
    Fake &f = *(Fake *)ctx;
    int what = f.call < f.script.size() ? f.script[f.call] : 0;
    f.call++;
    f.sizes.push_back(n);
    *done = 0;
    if (what == 0) {
        f.got.insert(f.got.end(), data, data + n);
        *done = n;
        return 0;
    }
    if (what == 2) {
        f.got.insert(f.got.end(), data, data + n / 2);
        *done = n / 2;
        return VTI_XFER_TIMEOUT;
    }
    return what == 1 ? VTI_XFER_TIMEOUT : -5;
}
std::vector<uint8_t> msg(int n)
{
    std::vector<uint8_t> m;
    for (int i = 0; i < n; i++)
        m.push_back((uint8_t)i);
    return m;
}
} // namespace

TEST(writeretry_packets_of_max)
{
    Fake f;
    auto m = msg(70);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, nullptr) == 0);
    CHECK(f.got == m);
    CHECK(f.sizes == std::vector<int>({32, 32, 6}));
}

TEST(writeretry_timeout_retries_same_packet)
{
    Fake f;
    f.script = {0, 1, 1, 0, 0}; // the second packet times out twice, then goes
    auto m = msg(70);
    int timeouts = 0;
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, &timeouts) == 0);
    CHECK(f.got == m); // nothing duplicated, nothing lost
    CHECK(timeouts == 2);
    CHECK(f.call == 5);
}

TEST(writeretry_gives_up_after_attempts)
{
    Fake f;
    f.script = {1, 1, 1, 1, 1, 1};
    auto m = msg(32);
    int timeouts = 0;
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, &timeouts) == -1);
    CHECK(timeouts == 5 && f.call == 5);
    CHECK(f.got.empty());
}

TEST(writeretry_error_stops_at_once)
{
    Fake f;
    f.script = {0, -1, 0};
    auto m = msg(64);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 32, 5, nullptr) == -1);
    CHECK(f.call == 2 && f.got.size() == 32);
}

TEST(writeretry_partial_then_timeout_continues)
{
    Fake f;
    f.script = {2, 0}; // WinUSB: half the message accepted before the timeout, the rest in the retry
    auto m = msg(64);
    CHECK(vti_write_chunks(&f, xfer, m.data(), (int)m.size(), 64, 5, nullptr) == 0);
    CHECK(f.got == m);
    CHECK(f.sizes == std::vector<int>({64, 32}));
}
