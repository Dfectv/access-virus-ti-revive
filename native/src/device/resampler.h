// resampler.h - the Virus' 44.1 kHz audio at any host rate: windowed-sinc interpolation (Kaiser window, 32 taps,
// 256 phases with linear interpolation between neighbouring phases). The caller hands in source frames and a fractional
// start position and step (source frames per output sample); the filter cutoff follows the step, so downsampling
// (hosts below 44.1 kHz) is band-limited too. No allocation after setup.
#pragma once
#include <cmath>
#include <vector>

namespace vti {

class Resampler {
public:
    static constexpr int TAPS = 32;   // source frames per output sample
    static constexpr int HALF = TAPS / 2;
    static constexpr int PHASES = 256;

    // step = source frames per output sample (44100 / host rate)
    void setup(double step)
    {
        const double cutoff = 0.5 * std::min(1.0, 1.0 / step) * 0.94; // cycles per source frame, below Nyquist
        const double beta = 8.6;                                       // Kaiser: ~-90 dB stop band
        table_.assign((size_t)(PHASES + 1) * TAPS, 0.0f);
        for (int ph = 0; ph <= PHASES; ph++) {
            double frac = (double)ph / PHASES, sum = 0;
            float *t = &table_[(size_t)ph * TAPS];
            for (int k = 0; k < TAPS; k++) {
                double x = (k - (HALF - 1)) - frac; // tap k sits at source frame floor(pos) - HALF + 1 + k
                double s = x == 0 ? 2 * cutoff : std::sin(2 * M_PI * cutoff * x) / (M_PI * x);
                double w = x / HALF;
                double win = std::fabs(w) >= 1 ? 0 : bessel0(beta * std::sqrt(1 - w * w)) / bessel0(beta);
                t[k] = (float)(s * win);
                sum += t[k];
            }
            for (int k = 0; k < TAPS; k++) // unity gain at DC for every phase
                t[k] = (float)(t[k] / sum);
        }
    }

    // Output sample i = the source signal at position pos + i * step, where src[0] is source frame `first`.
    // src must cover [floor(pos) - HALF + 1, floor(pos + (n - 1) * step) + HALF] (frames_needed).
    void process(const float *src, int64_t first, double pos, double step, float *out, int n) const
    {
        for (int i = 0; i < n; i++) {
            double p = pos + i * step;
            double fl = std::floor(p);
            double fp = (p - fl) * PHASES;
            int ph = (int)fp;
            float a = (float)(fp - ph);
            const float *t0 = &table_[(size_t)ph * TAPS], *t1 = t0 + TAPS;
            const float *s = src + ((int64_t)fl - HALF + 1 - first);
            float acc = 0;
            for (int k = 0; k < TAPS; k++)
                acc += s[k] * (t0[k] + a * (t1[k] - t0[k]));
            out[i] = acc;
        }
    }

    // the source range process() reads for n outputs from pos
    static void frames_needed(double pos, double step, int n, int64_t &first, int &count)
    {
        first = (int64_t)std::floor(pos) - HALF + 1;
        int64_t last = (int64_t)std::floor(pos + (n - 1) * step) + HALF;
        count = (int)(last - first + 1);
    }

private:
    std::vector<float> table_;
    static double bessel0(double x)
    {
        double sum = 1, term = 1;
        for (int k = 1; k < 40; k++) {
            term *= (x / (2 * k)) * (x / (2 * k));
            sum += term;
        }
        return sum;
    }
};

} // namespace vti
