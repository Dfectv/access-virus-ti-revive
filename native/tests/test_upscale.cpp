// upscale (skin option A): sizes, frames filtered without their neighbours, premultiplied alpha, factor 1, flat areas
#include "check.h"
#include "editor/upscale.h"
using namespace vti;

namespace {
Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    Image im;
    im.w = w, im.h = h;
    for (int i = 0; i < w * h; i++)
        im.px.insert(im.px.end(), {b, g, r, a}); // BGRA, alpha at index 3
    return im;
}
const uint8_t *at(const Image &im, int x, int y) { return &im.px[((size_t)y * im.w + x) * 4]; }
} // namespace

TEST(upscale_sizes)
{
    Image s = solid(129, 10, 10, 20, 30, 255);
    Image o = upscale(s, 2.0, 0, 3);
    CHECK(o.w == 258 && o.h == 20 && o.px.size() == (size_t)258 * 20 * 4);
    Image q = upscale(s, 1.5, 0, 3);
    CHECK(q.w == 194 && q.h == 15); // round(129 * 1.5) = 193.5 -> 194
    CHECK(upscale(Image{}, 2.0, 0, 3).w == 0);
    CHECK(upscale(s, 0, 0, 3).w == 0);
    // a flat area stays flat (the weights add up to one)
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++)
            CHECK(at(o, x, y)[0] == 30 && at(o, x, y)[1] == 20 && at(o, x, y)[2] == 10 && at(o, x, y)[3] == 255);
}

TEST(upscale_frames_do_not_bleed)
{
    // a filmstrip of two 8-row frames: black, then white - each enlarged frame keeps its own colour to the edge
    Image s = solid(4, 16, 0, 0, 0, 255);
    for (int y = 8; y < 16; y++)
        for (int x = 0; x < 4; x++)
            for (int c = 0; c < 3; c++)
                s.px[((size_t)y * 4 + x) * 4 + c] = 255;
    Image o = upscale(s, 2.0, 8, 3);
    CHECK(o.h == 32);
    CHECK(at(o, 0, 15)[0] == 0 && at(o, 3, 15)[2] == 0);    // the last row of frame 0: still black
    CHECK(at(o, 0, 16)[0] == 255 && at(o, 3, 16)[2] == 255); // the first row of frame 1: white
    // without frames the filter blends across the boundary
    Image b = upscale(s, 2.0, 0, 3);
    CHECK(at(b, 0, 15)[0] > 0 && at(b, 0, 16)[0] < 255);
}

TEST(upscale_premultiplied_and_identity)
{
    // a hard alpha edge: transparent left, opaque white right - Lanczos rings, colours must not exceed alpha
    Image s = solid(8, 2, 0, 0, 0, 0);
    for (int y = 0; y < 2; y++)
        for (int x = 4; x < 8; x++)
            for (int c = 0; c < 4; c++)
                s.px[((size_t)y * 8 + x) * 4 + c] = 255;
    Image o = upscale(s, 3.0, 0, 3);
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++)
            for (int c = 0; c < 3; c++)
                CHECK(at(o, x, y)[c] <= at(o, x, y)[3]);
    // factor 1: the same pixels (Lanczos interpolates at the sample positions exactly)
    Image g = solid(5, 5, 0, 0, 0, 255);
    for (int i = 0; i < 25; i++)
        g.px[(size_t)i * 4] = (uint8_t)(i * 10);
    Image same = upscale(g, 1.0, 0, 3);
    CHECK(same.px == g.px);
}
