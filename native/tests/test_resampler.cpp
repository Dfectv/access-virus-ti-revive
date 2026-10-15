// Resampler: a 44.1 kHz sine at other host rates against the exact sine, and block-wise = one-shot processing
#include "check.h"
#include "device/resampler.h"
#include <cmath>

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
