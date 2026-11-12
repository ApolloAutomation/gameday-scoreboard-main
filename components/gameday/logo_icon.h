// Row icons from decoded logos; pure, for host tests.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace espn {

// runtime_image's layout: RGB565 little endian, then an alpha plane.
inline size_t logo_icon_bytes(int w, int h) { return (size_t) w * h * 3; }

inline void logo_px_get(const uint8_t *img, int n, int i, int &r, int &g, int &b, int &a) {
  const uint16_t c = (uint16_t) (img[2 * i] | (img[2 * i + 1] << 8));
  r = ((c >> 11) & 0x1F) * 255 / 31;
  g = ((c >> 5) & 0x3F) * 255 / 63;
  b = (c & 0x1F) * 255 / 31;
  a = img[2 * n + i];
}

inline void logo_px_put(uint8_t *img, int n, int i, int r, int g, int b, int a) {
  auto c8 = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
  const uint16_t c = (uint16_t) (((c8(r) * 31 / 255) << 11) | ((c8(g) * 63 / 255) << 5) | (c8(b) * 31 / 255));
  img[2 * i] = (uint8_t) (c & 0xFF);
  img[2 * i + 1] = (uint8_t) (c >> 8);
  img[2 * n + i] = (uint8_t) c8(a);
}

inline int logo_luma(int r, int g, int b) { return (r * 77 + g * 150 + b * 29) >> 8; }

// Area average, alpha weighted, to px by px.
inline std::vector<uint8_t> logo_icon_shrink(const uint8_t *in, int w, int h, int px) {
  std::vector<uint8_t> out(logo_icon_bytes(px, px), 0);
  if (in == nullptr || w < 1 || h < 1 || px < 1)
    return out;
  for (int dy = 0; dy < px; dy++)
    for (int dx = 0; dx < px; dx++) {
      const int x0 = dx * w / px, x1 = std::max(x0 + 1, (dx + 1) * w / px);
      const int y0 = dy * h / px, y1 = std::max(y0 + 1, (dy + 1) * h / px);
      long sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
      for (int y = y0; y < y1 && y < h; y++)
        for (int x = x0; x < x1 && x < w; x++) {
          int r, g, b, a;
          logo_px_get(in, w * h, y * w + x, r, g, b, a);
          sr += r * a;
          sg += g * a;
          sb += b * a;
          sa += a;
          cnt++;
        }
      const int i = dy * px + dx;
      if (sa == 0 || cnt == 0)
        continue;
      logo_px_put(out.data(), px * px, i, (int) (sr / sa), (int) (sg / sa), (int) (sb / sa), (int) (sa / cnt));
    }
  return out;
}

// Dark vanishes on black: flip marks, brighten dim tiles.
inline bool logo_icon_lift(uint8_t *img, int w, int h) {
  const int n = w * h;
  long bright = 0, all = 0;
  int top = 0;
  for (int i = 0; i < n; i++) {
    int r, g, b, a;
    logo_px_get(img, n, i, r, g, b, a);
    const int y = std::max(r, std::max(g, b));
    all += a;
    if (y >= 96)
      bright += a;
    if (a >= 128 && y > top)
      top = y;
  }
  if (all == 0)
    return false;
  // A mark on clear, not a square tile
  const bool mark = all * 100 < (long) n * 255 * 85;
  const bool flip = mark && bright * 100 < all * 12;
  if (!flip && (top >= 160 || top < 8))
    return false;
  for (int i = 0; i < n; i++) {
    int r, g, b, a;
    logo_px_get(img, n, i, r, g, b, a);
    if (flip) {
      const int y = logo_luma(r, g, b), up = y < 128 ? 255 - 2 * y : 0;
      logo_px_put(img, n, i, r + up, g + up, b + up, a);
    } else {
      logo_px_put(img, n, i, r * 230 / top, g * 230 / top, b * 230 / top, a);
    }
  }
  return true;
}

}  // namespace espn
