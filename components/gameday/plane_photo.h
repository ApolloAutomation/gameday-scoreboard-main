// Plane photos cut, scaled and lit; pure, for tests.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace flights {

// At least this share of the height stays in.
static const int kPhotoKeepPct = 60;
// The background's brightness left, in 256ths.
static const int kPhotoDimLo = 64;
// Color distance where the plane is fully lit.
static const int kPhotoDimSpan = 96;
// Bigger pictures are refused before decoding.
static const uint32_t kPhotoMaxPixels = 1024u * 1024u;

// Where a decoded photo is cut, and its drawn size.
struct PhotoFit {
  int shift{0};      // decoded at 1 / (1 << shift)
  int sw{0}, sh{0};  // the decoded size
  int cx{0}, cy{0}, cw{0}, ch{0};  // the cut, decoded pixels
  int ow{0}, oh{0};  // drawn, within the box
  bool ok() const { return this->ow > 0 && this->oh > 0; }
};

// progressive: JPEGDEC reads its first scan alone, at 1/8.
inline PhotoFit photo_fit(uint32_t w, uint32_t h, bool progressive, int bw, int bh) {
  PhotoFit f;
  if (w == 0 || h == 0 || (uint64_t) w * h > kPhotoMaxPixels || bw < 1 || bh < 1)
    return f;
  // The whole width, cut toward the box's shape
  auto cut_h = [&](uint32_t sw, uint32_t sh) {
    const uint32_t box = (uint32_t) (((uint64_t) sw * bh + bw - 1) / bw);
    const uint32_t keep = (sh * kPhotoKeepPct + 99) / 100;
    return std::min(sh, std::max(box, keep));
  };
  auto drawn = [&](uint32_t cw, uint32_t ch, int &ow, int &oh) {
    if ((uint64_t) cw * bh >= (uint64_t) ch * bw) {
      ow = (int) std::min<uint32_t>(bw, cw);
      oh = (int) (((uint64_t) ch * ow + cw / 2) / cw);
    } else {
      oh = (int) std::min<uint32_t>(bh, ch);
      ow = (int) (((uint64_t) cw * oh + ch / 2) / ch);
    }
    ow = std::max(1, std::min(ow, bw));
    oh = std::max(1, std::min(oh, bh));
  };
  int shift = 3;
  if (!progressive) {
    int tw = 0, th = 0;
    drawn(w, cut_h(w, h), tw, th);
    // The smallest decode still as big as drawn
    while (shift > 0 && ((w >> shift) < (uint32_t) tw || (cut_h(w, h) >> shift) < (uint32_t) th))
      shift--;
  }
  f.shift = shift;
  f.sw = (int) ((w + (1u << shift) - 1) >> shift);
  f.sh = (int) ((h + (1u << shift) - 1) >> shift);
  f.cw = f.sw;
  f.ch = (int) cut_h((uint32_t) f.sw, (uint32_t) f.sh);
  // Mostly off the bottom: planes sit above the middle
  f.cy = (f.sh - f.ch) / 5;
  drawn((uint32_t) f.cw, (uint32_t) f.ch, f.ow, f.oh);
  return f;
}

struct PhotoRgb {
  int r{0}, g{0}, b{0};
};

inline PhotoRgb photo_rgb(uint16_t c) {
  const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return PhotoRgb{(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}

inline int photo_dist(const PhotoRgb &a, const PhotoRgb &b) {
  return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

inline PhotoRgb photo_mean2(const PhotoRgb &a, const PhotoRgb &b) {
  return PhotoRgb{(a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2};
}

// Unlike both row ends: the plane, not the sky.
inline int photo_stand_out(const PhotoRgb &c, const PhotoRgb &left, const PhotoRgb &right) {
  return std::min(photo_dist(c, left), photo_dist(c, right));
}

// The cut, area-averaged to the drawn size; out: 8-bit RGB.
inline void photo_scale(const uint16_t *px, const PhotoFit &f, uint8_t *out) {
  if (!f.ok())
    return;
  const float sx = (float) f.cw / f.ow, sy = (float) f.ch / f.oh;
  for (int oy = 0; oy < f.oh; oy++) {
    const float y0 = f.cy + oy * sy, y1 = y0 + sy;
    for (int ox = 0; ox < f.ow; ox++) {
      const float x0 = f.cx + ox * sx, x1 = x0 + sx;
      float r = 0, g = 0, b = 0, n = 0;
      for (int y = (int) y0; y < f.sh && (float) y < y1; y++) {
        const float wy = std::min((float) y + 1, y1) - std::max((float) y, y0);
        for (int x = (int) x0; x < f.sw && (float) x < x1; x++) {
          const float wt = wy * (std::min((float) x + 1, x1) - std::max((float) x, x0));
          if (wt <= 0)
            continue;
          const PhotoRgb c = photo_rgb(px[(size_t) y * f.sw + x]);
          r += c.r * wt;
          g += c.g * wt;
          b += c.b * wt;
          n += wt;
        }
      }
      uint8_t *o = &out[((size_t) oy * f.ow + ox) * 3];
      o[0] = n > 0 ? (uint8_t) (r / n + 0.5f) : 0;
      o[1] = n > 0 ? (uint8_t) (g / n + 0.5f) : 0;
      o[2] = n > 0 ? (uint8_t) (b / n + 0.5f) : 0;
    }
  }
}

// Dims what matches the row's ends; RGB565, low byte first.
inline void photo_light(const uint8_t *rgb, int w, int h, uint8_t *out) {
  for (int y = 0; y < h; y++) {
    const uint8_t *row = &rgb[(size_t) y * w * 3];
    auto at = [row](int x) { return PhotoRgb{row[x * 3], row[x * 3 + 1], row[x * 3 + 2]}; };
    const bool lit = w >= 4;
    const PhotoRgb left = lit ? photo_mean2(at(0), at(1)) : PhotoRgb{};
    const PhotoRgb right = lit ? photo_mean2(at(w - 1), at(w - 2)) : PhotoRgb{};
    for (int x = 0; x < w; x++) {
      const PhotoRgb c = at(x);
      int f = 256;
      if (lit) {
        // Smoothstep from kPhotoDimLo up to full
        const int t = std::min(photo_stand_out(c, left, right), kPhotoDimSpan) * 256 / kPhotoDimSpan;
        const int s = t * t * (768 - 2 * t) >> 16;
        f = kPhotoDimLo + ((256 - kPhotoDimLo) * s >> 8);
      }
      const int r = c.r * f >> 8, g = c.g * f >> 8, b = c.b * f >> 8;
      const uint16_t v = (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
      const size_t i = (size_t) y * w + x;
      out[2 * i] = (uint8_t) (v & 0xFF);
      out[2 * i + 1] = (uint8_t) (v >> 8);
    }
  }
}

}  // namespace flights
