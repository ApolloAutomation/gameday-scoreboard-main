// A PNG map shrunk into a box; pure.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace espn {

// Bigger maps are refused before decoding.
static const uint32_t kMapMaxSide = 1024;
// Half covered or more shows full color.
static const int kMapLift = 2;
// Faint colors raised to this, for LEDs.
static const uint32_t kMapVivid = 210;

// Sums per box pixel; saturating, never wrapping.
struct MapCell {
  uint16_t r, g, b, a, n;
};

// The middle cut to the box's shape, area-averaged over black.
struct MapCanvas {
  MapCell *cells{nullptr};  // bw * bh
  uint32_t sw{0}, sh{0};
  uint32_t cx{0}, cy{0}, cw{0}, ch{0};
  int bw{0}, bh{0};

  // False for a size it can't read.
  bool setup(uint32_t w, uint32_t h, int box_w, int box_h) {
    if (w == 0 || h == 0 || w > kMapMaxSide || h > kMapMaxSide || box_w < 1 || box_h < 1)
      return false;
    this->sw = w;
    this->sh = h;
    this->bw = box_w;
    this->bh = box_h;
    if ((uint64_t) w * box_h >= (uint64_t) h * box_w) {
      this->ch = h;
      this->cw = std::max<uint32_t>(1, (uint32_t) ((uint64_t) h * box_w / box_h));
    } else {
      this->cw = w;
      this->ch = std::max<uint32_t>(1, (uint32_t) ((uint64_t) w * box_h / box_w));
    }
    this->cx = (w - this->cw) / 2;
    this->cy = (h - this->ch) / 2;
    return true;
  }

  static void add(uint16_t &to, uint32_t v) { to = (uint16_t) std::min<uint32_t>(65535, to + v); }

  void paint(uint32_t x, uint32_t y, uint32_t rw, uint32_t rh, const uint8_t rgba[4]) {
    if (this->cells == nullptr)
      return;
    const uint32_t a = rgba[3];
    for (uint32_t yy = y; yy < y + rh && yy < this->cy + this->ch; yy++) {
      if (yy < this->cy)
        continue;
      const uint32_t oy = (yy - this->cy) * (uint32_t) this->bh / this->ch;
      for (uint32_t xx = x; xx < x + rw && xx < this->cx + this->cw; xx++) {
        if (xx < this->cx)
          continue;
        const uint32_t ox = (xx - this->cx) * (uint32_t) this->bw / this->cw;
        MapCell &c = this->cells[oy * (uint32_t) this->bw + ox];
        add(c.r, rgba[0] * a >> 8);
        add(c.g, rgba[1] * a >> 8);
        add(c.b, rgba[2] * a >> 8);
        add(c.a, a);
        add(c.n, 1);
      }
    }
  }

  // RGB565, low byte first, opaque.
  void finish(uint8_t *out) const {
    for (int i = 0; i < this->bw * this->bh; i++) {
      const MapCell &c = this->cells[i];
      uint32_t r = 0, g = 0, b = 0;
      if (c.a > 0 && c.n > 0) {
        // The color under the ink, then its coverage
        uint32_t lift = std::min<uint32_t>(255, (uint32_t) c.a * kMapLift / c.n);
        r = std::min<uint32_t>(255, (uint32_t) c.r * 256 / c.a);
        g = std::min<uint32_t>(255, (uint32_t) c.g * 256 / c.a);
        b = std::min<uint32_t>(255, (uint32_t) c.b * 256 / c.a);
        const uint32_t top = std::max(r, std::max(g, b));
        if (top > 0 && top < kMapVivid)
          lift = lift * kMapVivid / top;
        r = std::min<uint32_t>(255, r * lift / 255);
        g = std::min<uint32_t>(255, g * lift / 255);
        b = std::min<uint32_t>(255, b * lift / 255);
      }
      const uint16_t v = (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
      out[2 * i] = (uint8_t) (v & 0xFF);
      out[2 * i + 1] = (uint8_t) (v >> 8);
    }
  }
};

}  // namespace espn
