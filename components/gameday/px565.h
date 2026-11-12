// Pure RGB565 drawing for the play animations
#pragma once

#include <cstdint>

#include "sprites.h"

namespace espn {

struct PxBox {
  int16_t x{0}, y{0}, w{0}, h{0};
  bool empty() const { return w <= 0 || h <= 0; }
};

inline PxBox box_join(const PxBox &a, const PxBox &b) {
  if (a.empty())
    return b;
  if (b.empty())
    return a;
  const int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
  const int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  const int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return PxBox{(int16_t) x0, (int16_t) y0, (int16_t) (x1 - x0), (int16_t) (y1 - y0)};
}

inline PxBox box_clip(const PxBox &a, const PxBox &b) {
  const int x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
  const int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
  const int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
  if (x1 <= x0 || y1 <= y0)
    return PxBox{};
  return PxBox{(int16_t) x0, (int16_t) y0, (int16_t) (x1 - x0), (int16_t) (y1 - y0)};
}

inline bool box_overlap(const PxBox &a, const PxBox &b) { return !box_clip(a, b).empty(); }

constexpr uint16_t rgb565(uint32_t c) {
  return (uint16_t) ((((c >> 19) & 31) << 11) | (((c >> 10) & 63) << 5) | ((c >> 3) & 31));
}

// pct of the color, 0-100
constexpr uint32_t dim_rgb(uint32_t c, int pct) {
  return ((((c >> 16) & 255) * pct / 100) << 16) | ((((c >> 8) & 255) * pct / 100) << 8) | ((c & 255) * pct / 100);
}

// Draws through clip; mirror flips x across the picture
struct Px565 {
  uint16_t *px{nullptr};
  int16_t w{0}, h{0}, stride{0};
  PxBox clip{};
  bool mirror{false};

  void put(int x, int y, uint16_t c) {
    if (this->mirror)
      x = this->w - 1 - x;
    if (x < this->clip.x || y < this->clip.y || x >= this->clip.x + this->clip.w || y >= this->clip.y + this->clip.h)
      return;
    this->px[y * this->stride + x] = c;
  }
  void fill(int x, int y, int w, int h, uint16_t c) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        this->put(x + i, y + j, c);
  }
  void hline(int x, int y, int w, uint16_t c) { this->fill(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { this->fill(x, y, 1, h, c); }
};

// A logical box on the picture, after mirroring
inline PxBox px_phys(const Px565 &p, PxBox b) {
  if (p.mirror)
    b.x = (int16_t) (p.w - b.x - b.w);
  return box_clip(b, PxBox{0, 0, p.w, p.h});
}

// A sprite's pixels, anchor at (x, y)
inline PxBox sprite_box(const Sprite &s, int x, int y, int scale, bool flip) {
  const int ax = flip ? s.w - 1 - s.ax : s.ax;
  return PxBox{(int16_t) (x - ax * scale), (int16_t) (y - s.ay * scale), (int16_t) (s.w * scale),
               (int16_t) (s.h * scale)};
}

// Slot 0 is transparent; flip faces it left
inline void px_sprite(Px565 &p, const Sprite &s, int x, int y, const uint16_t pal[16], int scale, bool flip) {
  const PxBox b = sprite_box(s, x, y, scale, flip);
  for (int sy = 0; sy < s.h; sy++)
    for (int sx = 0; sx < s.w; sx++) {
      const int i = sy * s.w + sx;
      const uint8_t v = (s.px[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
      if (v == 0)
        continue;
      const int dx = flip ? s.w - 1 - sx : sx;
      p.fill(b.x + dx * scale, b.y + sy * scale, scale, scale, pal[v]);
    }
}

// 3x5 glyphs, rows top down, bit 2 the left column
inline const uint8_t *font3x5(char c) {
  static const char kChars[] = "0123456789FTLECNRIGH- ";
  static const uint8_t kRows[][5] = {
      {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
      {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {7, 4, 6, 4, 4}, {7, 2, 2, 2, 2},
      {4, 4, 4, 4, 7}, {7, 4, 6, 4, 7}, {7, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 6, 5, 5}, {7, 2, 2, 2, 7},
      {7, 4, 5, 5, 7}, {5, 5, 7, 5, 5}, {0, 0, 7, 0, 0}, {0, 0, 0, 0, 0}};
  for (int i = 0; kChars[i]; i++)
    if (kChars[i] == c)
      return kRows[i];
  return kRows[sizeof(kChars) - 2];
}

inline int px_text_w(const char *s, int scale) {
  int n = 0;
  while (s[n])
    n++;
  return n ? (4 * n - 1) * scale : 0;
}

// Left to right even when mirrored
inline void px_text(Px565 &p, int x, int y, const char *s, uint16_t c, int scale) {
  const bool m = p.mirror;
  if (m)
    x = p.w - x - px_text_w(s, scale);
  p.mirror = false;
  for (int k = 0; s[k]; k++) {
    const uint8_t *g = font3x5(s[k]);
    for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
        if (g[r] & (4 >> b))
          p.fill(x + (4 * k + b) * scale, y + r * scale, scale, scale, c);
  }
  p.mirror = m;
}

}  // namespace espn
