// Row icons from decoded logos; pure, for host tests.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "logo_lru.h"

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
// most: three quarters' level, so an accent can't hide it.
inline bool logo_icon_lift(uint8_t *img, int w, int h, bool most = false) {
  const int n = w * h;
  long bright = 0, all = 0;
  int opaque = 0;
  uint16_t hist[256] = {};
  for (int i = 0; i < n; i++) {
    int r, g, b, a;
    logo_px_get(img, n, i, r, g, b, a);
    const int y = std::max(r, std::max(g, b));
    all += a;
    if (y >= 96)
      bright += a;
    if (a >= 128) {
      hist[y]++;
      opaque++;
    }
  }
  if (all == 0)
    return false;
  int top = 0;
  if (most) {
    for (int k = std::min(opaque - 1, opaque * 3 / 4), seen = 0; top < 255; top++) {
      seen += hist[top];
      if (seen > k)
        break;
    }
  } else {
    for (top = 255; top > 0 && hist[top] == 0;)
      top--;
  }
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

// Centered on a clear square of side max(w, h).
inline void logo_icon_square(const uint8_t *in, int w, int h, uint8_t *out) {
  const int s = std::max(w, h), n = w * h, sn = s * s;
  memset(out, 0, logo_icon_bytes(s, s));
  const int ox = (s - w) / 2, oy = (s - h) / 2;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const int i = y * w + x, o = (y + oy) * s + x + ox;
      out[2 * o] = in[2 * i];
      out[2 * o + 1] = in[2 * i + 1];
      out[2 * sn + o] = in[2 * n + i];
    }
}

inline bool logo_whiteish(const uint8_t *img, int n, int i) {
  int r, g, b, a;
  logo_px_get(img, n, i, r, g, b, a);
  return a >= 128 && r >= 0xE0 && g >= 0xE0 && b >= 0xE0;
}

// White joined to the edge goes clear: no glaring tiles.
template<class V> int logo_icon_knockout(uint8_t *img, int w, int h, V &stack) {
  const int n = w * h;
  int cleared = 0;
  stack.clear();
  auto take = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h)
      return;
    const int i = y * w + x;
    if (!logo_whiteish(img, n, i))
      return;
    img[2 * n + i] = 0;
    cleared++;
    stack.push_back((uint16_t) i);
  };
  for (int x = 0; x < w; x++) {
    take(x, 0);
    take(x, h - 1);
  }
  for (int y = 0; y < h; y++) {
    take(0, y);
    take(w - 1, y);
  }
  while (!stack.empty()) {
    const int i = stack.back();
    stack.pop_back();
    const int x = i % w, y = i / w;
    take(x + 1, y);
    take(x - 1, y);
    take(x, y + 1);
    take(x, y - 1);
  }
  return cleared;
}

// Enough lit pixels, bright, wide and tall enough to read.
inline bool logo_icon_usable(const uint8_t *img, int px) {
  const int n = px * px;
  int lit = 0, mid = 0, bright = 0, x0 = px, x1 = -1, y0 = px, y1 = -1;
  for (int i = 0; i < n; i++) {
    int r, g, b, a;
    logo_px_get(img, n, i, r, g, b, a);
    const int m = std::max(r, std::max(g, b));
    if (a < 128 || m < 0x40)
      continue;
    lit++;
    mid += m >= 0x80;
    bright += m >= 0xA0;
    const int x = i % px, y = i / px;
    x0 = std::min(x0, x);
    x1 = std::max(x1, x);
    y0 = std::min(y0, y);
    y1 = std::max(y1, y);
  }
  // Some bright pixels, or mostly not dim
  const bool light = bright >= n / 16 || lit - mid <= lit / 2;
  return lit >= n * 5 / 64 && x1 - x0 + 1 >= px * 3 / 8 && y1 - y0 + 1 >= px / 4 && light;
}

// A PNG's IHDR color type, or -1.
inline int logo_png_color_type(const uint8_t *body, size_t n) {
  static const uint8_t sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (body == nullptr || n < 26 || memcmp(body, sig, 8) != 0 || memcmp(body + 12, "IHDR", 4) != 0)
    return -1;
  return body[25];
}

// Google's airline logos, by IATA code
static const char *const kGoogleLogoUrl = "https://www.gstatic.com/flights/airline_logos/32px/";
// logo_key of Google's 405-byte gray tail at 32 px.
static const uint32_t kGstaticBlankKey = 0x8cf55aa4u;

// Google answers unknown airlines with a gray tail.
inline bool logo_placeholder(const char *url, const uint8_t *body, size_t n) {
  if (url == nullptr || strncmp(url, kGoogleLogoUrl, strlen(kGoogleLogoUrl)) != 0)
    return false;
  return logo_key(reinterpret_cast<const char *>(body), n) == kGstaticBlankKey || logo_png_color_type(body, n) == 4;
}

}  // namespace espn
