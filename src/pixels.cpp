#include "pixels.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace hq {
Image::Image(int w, int h, int bits) : width(w), height(h), bpp(bits) {
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 ||
        (bits != 8 && bits != 16 && bits != 32))
        throw std::invalid_argument("unsupported surface size/format");
    pitch = (w * (bits / 8) + 3) & ~3;
    const size_t size = size_t(pitch) * h;
    if (size > 256u * 1024 * 1024) throw std::invalid_argument("surface too large");
    bytes.resize(size);
}
uint32_t Image::read(int x, int y) const {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + size_t(y) * pitch + x * (bpp / 8), bpp / 8);
    return value;
}
void Image::write(int x, int y, uint32_t value) {
    std::memcpy(bytes.data() + size_t(y) * pitch + x * (bpp / 8), &value, bpp / 8);
}
std::vector<uint32_t> rgb(const Image& s, const Palette& palette) {
    std::vector<uint32_t> out(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; ++y) for (int x = 0; x < s.width; ++x) {
        auto v = s.read(x, y);
        if (s.bpp == 8) v = palette[v];
        else if (s.bpp == 16) {
            const auto r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            v = (((r << 3) | (r >> 2)) << 16) |
                (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
        }
        out[size_t(y) * s.width + x] = v & 0xffffff;
    }
    return out;
}
bool valid_rect(Rect r) {
    return r.right > r.left && r.bottom > r.top &&
           int64_t(r.right) - r.left <= 65536 && int64_t(r.bottom) - r.top <= 65536;
}
void fill(Image& d, Rect r, uint32_t color) {
    for (int y = std::max(0, r.top); y < std::min(d.height, r.bottom); ++y)
        for (int x = std::max(0, r.left); x < std::min(d.width, r.right); ++x)
            d.write(x, y, color);
}
void blit(Image& d, Rect dr, const Image& s, Rect sr,
          bool sk, uint32_t sl, uint32_t sh, bool dk, uint32_t dl, uint32_t dh,
          bool mx, bool my) {
    if (!valid_rect(dr) || !valid_rect(sr) || d.bpp != s.bpp)
        throw std::invalid_argument("invalid blit");
    // Snapshot overlapping self-blits so reads cannot see pixels already written.
    if (&d == &s) {
        Image copy = s;
        blit(d, dr, copy, sr, sk, sl, sh, dk, dl, dh, mx, my);
        return;
    }
    for (int y = std::max(0, dr.top); y < std::min(d.height, dr.bottom); ++y) {
        int sy = int((int64_t(y) - dr.top) * (sr.bottom - sr.top) / (dr.bottom - dr.top));
        sy = my ? sr.bottom - 1 - sy : sr.top + sy;
        if (sy < 0 || sy >= s.height) continue;
        for (int x = std::max(0, dr.left); x < std::min(d.width, dr.right); ++x) {
            int sx = int((int64_t(x) - dr.left) * (sr.right - sr.left) / (dr.right - dr.left));
            sx = mx ? sr.right - 1 - sx : sr.left + sx;
            if (sx < 0 || sx >= s.width) continue;
            const auto v = s.read(sx, sy), old = d.read(x, y);
            if (sk && v >= sl && v <= sh) continue;
            if (dk && (old < dl || old > dh)) continue;
            d.write(x, y, v);
        }
    }
}
}
