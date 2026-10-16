// upscale.cpp - see upscale.h. Separable Lanczos-3: horizontal pass over the whole width, vertical pass per frame.
#include "upscale.h"
#include <algorithm>
#include <cmath>

namespace vti {

namespace {
constexpr double PI = 3.14159265358979323846;

double lanczos3(double x)
{
    x = std::fabs(x);
    if (x < 1e-9)
        return 1.0;
    if (x >= 3.0)
        return 0.0;
    const double px = PI * x;
    return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}

// for every destination index in [0, dn): the source taps in [0, sn) and their normalized weights (sampling an
// enlargement: the filter keeps its width of 3 source pixels)
struct Taps {
    std::vector<int> first, count;
    std::vector<float> w; // count[i] weights per destination, packed
    std::vector<size_t> at;
};

Taps taps(int sn, int dn)
{
    Taps t;
    t.first.resize((size_t)dn), t.count.resize((size_t)dn), t.at.resize((size_t)dn);
    const double scale = (double)sn / dn;
    for (int i = 0; i < dn; i++) {
        const double c = (i + 0.5) * scale - 0.5; // the source position of the destination pixel's centre
        int a = (int)std::floor(c) - 2, b = (int)std::floor(c) + 3;
        a = std::max(a, 0), b = std::min(b, sn - 1);
        double sum = 0;
        t.at[(size_t)i] = t.w.size();
        for (int k = a; k <= b; k++) {
            const double v = lanczos3(k - c);
            t.w.push_back((float)v);
            sum += v;
        }
        for (size_t k = t.at[(size_t)i]; k < t.w.size(); k++)
            t.w[k] = (float)(t.w[k] / (sum != 0 ? sum : 1));
        t.first[(size_t)i] = a, t.count[(size_t)i] = b - a + 1;
    }
    return t;
}

inline uint8_t clamp8(float v) { return (uint8_t)std::lround(std::min(255.0f, std::max(0.0f, v))); }
} // namespace

Image upscale(const Image &src, double factor, int frame_h, int alpha)
{
    Image out;
    if (src.w <= 0 || src.h <= 0 || factor <= 0 || src.px.size() < (size_t)src.w * src.h * 4)
        return out;
    out.w = std::max(1, (int)std::lround(src.w * factor));
    out.h = std::max(1, (int)std::lround(src.h * factor));
    out.px.assign((size_t)out.w * out.h * 4, 0);
    if (frame_h <= 0 || frame_h > src.h)
        frame_h = src.h;
    // horizontal pass: every source row to the new width (floats, so the vertical pass works on unrounded values)
    const Taps tx = taps(src.w, out.w);
    std::vector<float> mid((size_t)out.w * src.h * 4);
    for (int y = 0; y < src.h; y++) {
        const uint8_t *row = &src.px[(size_t)y * src.w * 4];
        float *o = &mid[(size_t)y * out.w * 4];
        for (int x = 0; x < out.w; x++) {
            float acc[4] = {0, 0, 0, 0};
            const float *w = &tx.w[tx.at[(size_t)x]];
            for (int k = 0; k < tx.count[(size_t)x]; k++) {
                const uint8_t *p = row + (size_t)(tx.first[(size_t)x] + k) * 4;
                for (int c = 0; c < 4; c++)
                    acc[c] += w[k] * p[c];
            }
            for (int c = 0; c < 4; c++)
                o[(size_t)x * 4 + c] = acc[c];
        }
    }
    // vertical pass frame by frame: rows of frame k only ever read rows of frame k
    for (int f0 = 0; f0 < src.h; f0 += frame_h) {
        const int fh = std::min(frame_h, src.h - f0);
        const int d0 = (int)std::lround(f0 * factor), d1 = std::min(out.h, (int)std::lround((f0 + fh) * factor));
        if (d1 <= d0)
            continue;
        const Taps ty = taps(fh, d1 - d0);
        for (int y = 0; y < d1 - d0; y++) {
            uint8_t *o = &out.px[(size_t)(d0 + y) * out.w * 4];
            const float *w = &ty.w[ty.at[(size_t)y]];
            for (int x = 0; x < out.w; x++) {
                float acc[4] = {0, 0, 0, 0};
                for (int k = 0; k < ty.count[(size_t)y]; k++) {
                    const float *p = &mid[((size_t)(f0 + ty.first[(size_t)y] + k) * out.w + x) * 4];
                    for (int c = 0; c < 4; c++)
                        acc[c] += w[k] * p[c];
                }
                uint8_t *q = o + (size_t)x * 4;
                for (int c = 0; c < 4; c++)
                    q[c] = clamp8(acc[c]);
                if (alpha >= 0 && alpha < 4) // premultiplied: no colour above its alpha (Lanczos rings a little)
                    for (int c = 0; c < 4; c++)
                        if (c != alpha && q[c] > q[alpha])
                            q[c] = q[alpha];
            }
        }
    }
    return out;
}

} // namespace vti
