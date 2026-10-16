// upscale.h - sharper editor images for scaled displays (skin option A): the user's own Access bitmaps, enlarged once
// with a Lanczos-3 filter to the scale they are drawn at (display scaling x editor size), kept in memory only.
// Filmstrips (knob frames, font glyphs stacked vertically) are resampled frame by frame, so nothing of the next frame
// bleeds in at the edges.
#pragma once
#include <cstdint>
#include <vector>

namespace vti {

// 4 bytes per pixel in any channel order; `alpha` = the index of the alpha byte (0..3), or -1 for none. Premultiplied
// pixels (cairo, Direct2D, Core Graphics) stay valid: the colour channels are clamped to the alpha after filtering.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // w * h * 4, rows without padding
};

// `src` enlarged by `factor` (> 0): dw = round(w * factor), dh = round(h * factor). `frame_h` (> 0) splits the
// image into frames of that height (the last one may be shorter); each maps to rows round(k*fh*factor) ..
// round((k+1)*fh*factor) of the result and is filtered without its neighbours.
Image upscale(const Image &src, double factor, int frame_h, int alpha);

} // namespace vti
