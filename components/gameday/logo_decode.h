// PNG logo decoding steps; pure, for host tests.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace espn {

// Biggest source decoded; real logos reach 500x500.
static const uint32_t kLogoMaxPixels = 1024u * 1024u;
// The logo task rests a tick after this.
static const uint32_t kLogoSliceMs = 10;

inline uint32_t png_be32(const uint8_t *p) {
  return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | (uint32_t) p[3];
}

// Width and height from the IHDR; false if not PNG.
inline bool png_size(const uint8_t *b, size_t n, uint32_t &w, uint32_t &h) {
  static const uint8_t kHead[16] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
  if (b == nullptr || n < 24 || !std::equal(kHead, kHead + 16, b))
    return false;
  w = png_be32(b + 16);
  h = png_be32(b + 20);
  return w > 0 && h > 0;
}

inline bool logo_size_ok(uint32_t w, uint32_t h) { return w > 0 && h > 0 && (uint64_t) w * h <= kLogoMaxPixels; }

// Fits w x h in a box, keeping its shape.
inline void logo_fit(uint32_t w, uint32_t h, int box, int &fw, int &fh) {
  fw = fh = box;
  if (w > h)
    fh = std::max(1, (int) ((uint64_t) h * box / w));
  else if (h > w)
    fw = std::max(1, (int) ((uint64_t) w * box / h));
}

// Decoded pixels, point sampled into RGB565 then alpha.
struct LogoCanvas {
  uint8_t *px{nullptr};  // fw * fh * 3 bytes
  uint32_t w{0}, h{0};   // the source, within logo_size_ok
  int fw{0}, fh{0};

  // Paints one source rectangle, as runtime_image's draw did.
  void paint(uint32_t x, uint32_t y, uint32_t rw, uint32_t rh, const uint8_t rgba[4]) {
    if (this->px == nullptr || x >= this->w || y >= this->h)
      return;
    const uint32_t fw = this->fw, fh = this->fh;
    const uint32_t x0 = x * fw / this->w, x1 = std::min(fw, ((x + rw) * fw + this->w - 1) / this->w);
    const uint32_t y0 = y * fh / this->h, y1 = std::min(fh, ((y + rh) * fh + this->h - 1) / this->h);
    const uint16_t c = (uint16_t) ((rgba[0] >> 3) << 11 | (rgba[1] >> 2) << 5 | rgba[2] >> 3);
    uint8_t *alpha = this->px + 2 * fw * fh;
    for (uint32_t j = y0; j < y1; j++)
      for (uint32_t i = x0; i < x1; i++) {
        const uint32_t p = j * fw + i;
        this->px[2 * p] = (uint8_t) (c & 0xFF);
        this->px[2 * p + 1] = (uint8_t) (c >> 8);
        alpha[p] = rgba[3];
      }
  }
};

inline bool logo_rest_due(uint32_t since_ms, uint32_t now) { return now - since_ms >= kLogoSliceMs; }

}  // namespace espn
