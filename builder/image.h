// Small RGBA/RGB/gray image type with exactly the operations the art pipeline needs.
#pragma once
#include "util.h"
#include <array>

namespace hb {
struct Image {
    int w = 0, h = 0, c = 4;  // channels: 1, 3 or 4
    Bytes px;
    Image() = default;
    Image(int w_, int h_, int c_ = 4, uint8_t fill = 0) : w(w_), h(h_), c(c_), px(size_t(w_) * h_ * c_, fill) {}
    uint8_t* at(int x, int y) { return px.data() + (size_t(y) * w + x) * c; }
    const uint8_t* at(int x, int y) const { return px.data() + (size_t(y) * w + x) * c; }
    bool empty() const { return w == 0 || h == 0; }
};

Image load_png(const fs::path& path, int channels = 0);  // 0 keeps the file's channel count
Image load_png_memory(const Bytes& data, int channels = 0);
void save_png(const Image& image, const fs::path& path);  // atomic
Bytes encode_png(const Image& image);

Image convert(const Image& image, int channels);  // gray/RGB/RGBA, alpha 255 when added
Image resize_nearest(const Image& image, int w, int h);
Image crop(const Image& image, int x, int y, int w, int h);  // out of range pixels become zero
void paste(Image& dst, const Image& src, int x, int y);     // replaces pixels, like PIL paste without mask
void alpha_composite(Image& dst, const Image& src);         // both RGBA, same size, "over"
Image pad_edge(const Image& image, int n);                  // replicate edge pixels
Image pad_reflect101(const Image& image, int n);            // gfedcb|abcdefgh|gfedcba
// Bounding box of nonzero alpha (or nonzero gray): {x0, y0, x1, y1} exclusive; empty when none.
bool bbox(const Image& image, std::array<int, 4>& box);

// For every pixel, the index (y*w+x) of the nearest pixel where `set` is true (Euclidean).
std::vector<int32_t> nearest_set(const std::vector<uint8_t>& set, int w, int h);
}
