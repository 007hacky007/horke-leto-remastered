#include "image.h"
#include <algorithm>
#include <cstring>
#include <limits>

#include "stb_image.h"
#include "stb_image_write.h"

namespace hb {
Image load_png_memory(const Bytes& data, int channels) {
    int w = 0, h = 0, n = 0;
    uint8_t* pixels = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &n, channels);
    if (!pixels) throw std::runtime_error(std::string("Vadný obrázek PNG: ") + stbi_failure_reason());
    Image out; out.w = w; out.h = h; out.c = channels ? channels : n;
    out.px.assign(pixels, pixels + size_t(w) * h * out.c);
    stbi_image_free(pixels);
    return out;
}

Image load_png(const fs::path& path, int channels) {
    try {
        return load_png_memory(read_file(path), channels);
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

static void collect(void* context, void* data, int size) {
    auto* out = static_cast<Bytes*>(context);
    out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}

Bytes encode_png(const Image& image) {
    Bytes out;
    stbi_write_png_compression_level = 6;
    if (!stbi_write_png_to_func(collect, &out, image.w, image.h, image.c, image.px.data(), image.w * image.c))
        throw std::runtime_error("PNG se nepodařilo zakódovat");
    return out;
}

void save_png(const Image& image, const fs::path& path) {
    auto data = encode_png(image);
    write_file(path, data.data(), data.size());
}

Image convert(const Image& in, int channels) {
    if (in.c == channels) return in;
    Image out(in.w, in.h, channels);
    for (size_t i = 0, n = size_t(in.w) * in.h; i < n; ++i) {
        const uint8_t* s = &in.px[i * in.c];
        uint8_t* d = &out.px[i * channels];
        uint8_t r, g, b, a = 255;
        if (in.c == 1) { r = g = b = s[0]; }
        else { r = s[0]; g = s[1]; b = s[2]; if (in.c == 4) a = s[3]; }
        if (channels == 1) d[0] = uint8_t((r * 299 + g * 587 + b * 114 + 500) / 1000);
        else { d[0] = r; d[1] = g; d[2] = b; if (channels == 4) d[3] = a; }
    }
    return out;
}

Image resize_nearest(const Image& in, int w, int h) {
    Image out(w, h, in.c);
    for (int y = 0; y < h; ++y) {
        int sy = std::min(in.h - 1, int((y + 0.5) * in.h / h));
        for (int x = 0; x < w; ++x) {
            int sx = std::min(in.w - 1, int((x + 0.5) * in.w / w));
            std::memcpy(out.at(x, y), in.at(sx, sy), in.c);
        }
    }
    return out;
}

Image crop(const Image& in, int x0, int y0, int w, int h) {
    Image out(w, h, in.c);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sx = x0 + x, sy = y0 + y;
            if (sx >= 0 && sy >= 0 && sx < in.w && sy < in.h) std::memcpy(out.at(x, y), in.at(sx, sy), in.c);
        }
    return out;
}

void paste(Image& dst, const Image& src, int x0, int y0) {
    Image s = convert(src, dst.c);
    for (int y = 0; y < s.h; ++y) {
        int dy = y0 + y;
        if (dy < 0 || dy >= dst.h) continue;
        for (int x = 0; x < s.w; ++x) {
            int dx = x0 + x;
            if (dx >= 0 && dx < dst.w) std::memcpy(dst.at(dx, dy), s.at(x, y), dst.c);
        }
    }
}

// PIL Image.alpha_composite, 8-bit straight alpha.
void alpha_composite(Image& dst, const Image& src) {
    for (size_t i = 0, n = size_t(dst.w) * dst.h; i < n; ++i) {
        uint8_t* d = &dst.px[i * 4];
        const uint8_t* s = &src.px[i * 4];
        if (s[3] == 0) continue;
        if (s[3] == 255) { std::memcpy(d, s, 4); continue; }
        double sa = s[3] / 255.0, da = d[3] / 255.0, oa = sa + da * (1 - sa);
        for (int k = 0; k < 3; ++k)
            d[k] = uint8_t(std::clamp((s[k] * sa + d[k] * da * (1 - sa)) / oa + 0.5, 0.0, 255.0));
        d[3] = uint8_t(oa * 255 + 0.5);
    }
}

static Image pad(const Image& in, int n, bool reflect) {
    Image out(in.w + 2 * n, in.h + 2 * n, in.c);
    auto map = [&](int v, int size) {
        if (!reflect) return std::clamp(v, 0, size - 1);
        if (size == 1) return 0;
        while (v < 0 || v >= size) v = v < 0 ? -v : 2 * (size - 1) - v;
        return v;
    };
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            std::memcpy(out.at(x, y), in.at(map(x - n, in.w), map(y - n, in.h)), in.c);
    return out;
}
Image pad_edge(const Image& in, int n) { return pad(in, n, false); }
Image pad_reflect101(const Image& in, int n) { return pad(in, n, true); }

bool bbox(const Image& in, std::array<int, 4>& box) {
    int ch = in.c == 4 ? 3 : 0;
    int x0 = in.w, y0 = in.h, x1 = -1, y1 = -1;
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x)
            if (in.at(x, y)[ch]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < 0) return false;
    box = {x0, y0, x1 + 1, y1 + 1};
    return true;
}

// Felzenszwalb and Huttenlocher's exact squared Euclidean transform, tracking the source index.
static void transform_1d(const std::vector<double>& f, const std::vector<int32_t>& fi, int n,
                         std::vector<double>& d, std::vector<int32_t>& di) {
    std::vector<int> v(n);
    std::vector<double> z(n + 1);
    const double inf = std::numeric_limits<double>::infinity();
    int k = -1;
    for (int q = 0; q < n; ++q) {
        if (f[q] == inf) continue;
        if (k < 0) { k = 0; v[0] = q; z[0] = -inf; z[1] = inf; continue; }
        double s;
        while (true) {
            s = ((f[q] + double(q) * q) - (f[v[k]] + double(v[k]) * v[k])) / (2.0 * q - 2.0 * v[k]);
            if (s <= z[k] && k > 0) --k; else break;
        }
        if (s <= z[k]) { v[k] = q; z[k + 1] = inf; continue; }  // only when k == 0
        ++k; v[k] = q; z[k] = s; z[k + 1] = inf;
    }
    if (k < 0) { std::fill(d.begin(), d.begin() + n, inf); std::fill(di.begin(), di.begin() + n, -1); return; }
    int j = 0;
    for (int q = 0; q < n; ++q) {
        while (z[j + 1] < q) ++j;
        d[q] = double(q - v[j]) * (q - v[j]) + f[v[j]];
        di[q] = fi[v[j]];
    }
}

std::vector<int32_t> nearest_set(const std::vector<uint8_t>& set, int w, int h) {
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(size_t(w) * h);
    std::vector<int32_t> idx(size_t(w) * h);
    int n = std::max(w, h);
    std::vector<double> f(n), d(n);
    std::vector<int32_t> fi(n), di(n);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) { size_t i = size_t(y) * w + x; f[y] = set[i] ? 0 : inf; fi[y] = set[i] ? int32_t(i) : -1; }
        transform_1d(f, fi, h, d, di);
        for (int y = 0; y < h; ++y) { dist[size_t(y) * w + x] = d[y]; idx[size_t(y) * w + x] = di[y]; }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) { f[x] = dist[size_t(y) * w + x]; fi[x] = idx[size_t(y) * w + x]; }
        transform_1d(f, fi, w, d, di);
        for (int x = 0; x < w; ++x) idx[size_t(y) * w + x] = di[x];
    }
    return idx;
}
}
