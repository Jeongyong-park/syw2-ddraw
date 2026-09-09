#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hq {
struct Rect { int left, top, right, bottom; };
struct Image {
    int width = 0, height = 0, bpp = 0, pitch = 0;
    std::vector<uint8_t> bytes;
    Image(int w, int h, int bits);
    uint32_t read(int x, int y) const;
    void write(int x, int y, uint32_t value);
};
// Pixel value is 0x00RRGGBB (little-endian DIB memory is B,G,R,0).
using Palette = std::array<uint32_t, 256>;
std::vector<uint32_t> rgb(const Image& source, const Palette& palette);
bool valid_rect(Rect r);
void fill(Image& target, Rect dest, uint32_t color);
void blit(Image& target, Rect dest, const Image& source, Rect src,
          bool key_source, uint32_t src_low, uint32_t src_high,
          bool key_dest, uint32_t dst_low, uint32_t dst_high,
          bool mirror_x = false, bool mirror_y = false);
}
