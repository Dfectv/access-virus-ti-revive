// Resampler: a 44.1 kHz sine at other host rates against the exact sine, and block-wise = one-shot processing
#include "check.h"
#include "device/resampler.h"
#include <cmath>
#include <cstdint>
#include <cstring>

using namespace vti;

static double worst_error(double host_rate, double freq)
{
    const double step = 44100.0 / host_rate;
    Resampler r;
    r.setup(step);
    std::vector<float> src(44100);
    for (size_t i = 0; i < src.size(); i++)
        src[i] = (float)(0.5 * std::sin(2 * Resampler::PI * freq * i / 44100.0));
    double worst = 0, pos = 1000.3; // fractional start
    for (int block = 0; block < 40; block++) { // 40 blocks of 256: continuity across blocks
        float out[256];
        int64_t first;
        int count;
        Resampler::frames_needed(pos, step, 256, first, count);
        r.process(src.data() + first, first, pos, step, out, 256);
        for (int i = 0; i < 256; i++) {
            double t = (pos + i * step) / 44100.0;
            worst = std::max(worst, std::fabs(out[i] - 0.5 * std::sin(2 * Resampler::PI * freq * t)));
        }
        pos += 256 * step;
    }
    return worst;
}

TEST(resampler_sine_accuracy)
{
    for (double rate : {48000.0, 88200.0, 96000.0, 32000.0}) {
        double e1 = worst_error(rate, 1000), e2 = worst_error(rate, 10000);
        std::printf("  %6.0f Hz: 1 kHz %.1f dB, 10 kHz %.1f dB\n", rate, 20 * std::log10(e1 / 0.5),
                    20 * std::log10(e2 / 0.5));
        CHECK(e1 < 0.5 * 1e-4); // better than -80 dB
        CHECK(e2 < 0.5 * 1e-3); // better than -60 dB near the top of the band
    }
}

TEST(resampler_unity_step)
{
    Resampler r;
    r.setup(1.0);
    std::vector<float> src(200);
    for (size_t i = 0; i < src.size(); i++)
        src[i] = (float)(0.3 * std::sin(i * 0.37) + 0.2 * std::sin(i * 1.9)); // 2.6 and 13.3 kHz
    float out[64];
    r.process(src.data(), 0, 50.0, 1.0, out, 64); // integer positions: the (band-limited) input itself
    double worst = 0;
    for (int i = 0; i < 64; i++)
        worst = std::max(worst, (double)std::fabs(out[i] - src[50 + i]));
    CHECK(worst < 1e-3);
}

// Mutation run 2026-10-20: frames_needed is exactly the range process() reads (more = latency the ring must cover,
// less = reads past the data), and process() writes n outputs, not n + 1. NaN marks every frame outside the range:
// one read of it makes an output NaN (also with a tap weight of 0).
// NaN from the bits: under fast-math (the SDK's Mac and Windows flags) std::isnan/isfinite may be folded to constants
static bool nan_bits(float v)
{
    uint32_t b;
    std::memcpy(&b, &v, sizeof b);
    return (b & 0x7FFFFFFF) > 0x7F800000;
}

TEST(resampler_reads_exactly_frames_needed)
{
    Resampler r;
    for (double step : {44100.0 / 48000, 1.0, 44100.0 / 96000}) {
        r.setup(step);
        for (double pos : {500.0, 500.37, 500.999}) {
            const int n = 64, G = 64;
            int64_t first;
            int count;
            Resampler::frames_needed(pos, step, n, first, count);
            std::vector<float> buf((size_t)count + 2 * G, NAN);
            for (int i = 0; i < count; i++)
                buf[(size_t)(G + i)] = 0.25f;
            float out[n + 1];
            out[n] = 7.0f;
            r.process(buf.data() + G, first, pos, step, out, n);
            bool finite = true;
            for (int i = 0; i < n; i++)
                finite = finite && !nan_bits(out[i]);
            CHECK(finite);       // nothing outside the range was read
            CHECK(out[n] == 7.0f); // nothing written past n
            for (int edge : {G, G + count - 1}) { // the first and the last frame of the range are both read
                buf[(size_t)edge] = NAN;
                r.process(buf.data() + G, first, pos, step, out, n);
                bool nan = false;
                for (int i = 0; i < n; i++)
                    nan = nan || nan_bits(out[i]);
                CHECK(nan);
                buf[(size_t)edge] = 0.25f;
            }
        }
    }
}
