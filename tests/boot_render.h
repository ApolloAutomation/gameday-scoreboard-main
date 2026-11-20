// The start-up screen drawn as boot.yaml lays it out
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "boot_brand.h"
#include "field_render.h"

namespace espn {

struct BootShot {
  int w{64}, h{64};
  const char *lines[4]{"", "", "", ""};
  bool busy{true};
  int pct{60};
  int dog{1};  // boot_view's dog request
  bool qr{false};
  uint32_t since_ms{1}, now_ms{1};
  // Code modules, top row first; empty draws a stand-in
  std::vector<std::vector<bool>> code;
};

inline Canvas boot_render(const BootShot &s, const ReviewFonts &fonts) {
  using namespace esphome::gameday;
  Canvas cv;
  cv.w = s.w;
  cv.h = s.h;
  cv.px.assign((size_t) s.w * s.h, rgb565(kBootBg));
  const bool want = boot_dog_wanted(s.w, s.h, s.dog, s.qr, s.since_ms, s.now_ms);
  const bool tall = s.lines[3][0] == 0;
  const BootBoxes b = boot_boxes(s.w, s.h, s.qr, want, tall);
  if (!b.dog.empty()) {
    std::vector<uint16_t> buf((size_t) kDogSize * kDogSize);
    Px565 p;
    p.px = buf.data();
    p.w = p.h = p.stride = kDogSize;
    p.clip = PxBox{0, 0, kDogSize, kDogSize};
    paint_apollo(p);
    for (int y = 0; y < kDogSize; y++)
      for (int x = 0; x < kDogSize; x++)
        cv.put(b.dog.x + x, b.dog.y + y, buf[(size_t) y * kDogSize + x]);
  }
  auto text = [&](int x, int y, int w, BoardFont f, uint32_t color, const std::string &v) {
    AppText t;
    t.x = (int16_t) x;
    t.y = (int16_t) y;
    t.w = (int16_t) w;
    t.font = f;
    t.color = color;
    t.text = v;
    // A line too long scrolls from its start
    const int tw = fonts.f[(int) f].ok() ? bdf_width(fonts.f[(int) f], v) : (int) v.size() * font_px(f);
    t.align = tw > w ? AppAlign::LEFT : AppAlign::CENTER;
    review_text(cv, fonts, t, false);
  };
  if (b.mode != BootWords::NONE) {
    const int ty = b.y0 + boot_title_y(b.mode, tall);
    if (boot_title_split(b.mode, tall)) {
      const BoardFont f = b.mode == BootWords::BESIDE ? BoardFont::S16 : BoardFont::S12;
      text(b.x0, ty, b.cw, f, kBrandLight, "APOLLO");
      text(b.x0, ty + font_rows(f), b.cw, f, kBrandLight, "PULSE");
    } else {
      text(b.x0, ty, b.cw, b.mode == BootWords::WIDE ? BoardFont::S16 : BoardFont::S8, kBrandLight,
           "APOLLO PULSE");
    }
    for (int i = 0; i < 4; i++) {
      const int y = boot_line_y(b.mode, i, tall);
      if (y < 0)
        continue;
      const BoardFont f = i == 1 && b.mode == BootWords::WIDE ? BoardFont::S12 : BoardFont::S8;
      text(b.x0 + 2, b.y0 + y, b.cw - 4, f, i == 1 ? 0xFFFFFF : kBrandLight, s.lines[i]);
    }
  }
  if (s.busy) {
    for (int y = 0; y < b.bar.h; y++)
      for (int x = 0; x < b.bar.w; x++)
        cv.put(b.bar.x + x, b.bar.y + y, rgb565(x < b.bar.w * s.pct / 100 ? kBrandGreen : kBrandBlue));
  } else if (b.mode != BootWords::NONE) {
    for (int x = 0; x < b.bar.w; x++)
      cv.put(b.bar.x + x, b.rule_y, rgb565(kBrandOrange));
  }
  if (!b.qr.empty()) {
    for (int y = 0; y < kDogSize; y++)
      for (int x = 0; x < kDogSize; x++)
        cv.put(b.qr.x + x, b.qr.y + y, 0);
    // 33 px canvas centered, the code centered in it
    const int n = s.code.empty() ? 29 : (int) s.code.size();
    const int ox = b.qr.x + (kDogSize - 33) / 2 + (33 - n) / 2, oy = b.qr.y + (kDogSize - 33) / 2 + (33 - n) / 2;
    for (int j = 0; j < n; j++)
      for (int i = 0; i < n; i++)
        if (s.code.empty() ? ((i * 7 + j * 3) % 5 < 3) : s.code[j][i])
          cv.put(ox + i, oy + j, 0xFFFF);
  }
  return cv;
}

// Linear LED drive, all white is 1.0 (CIE 1931)
inline double boot_drive(const Canvas &cv) {
  auto lin = [](double v) {
    const double l = v * 100.0;
    return l <= 8.0 ? l / 903.3 : std::pow((l + 16.0) / 116.0, 3.0);
  };
  double sum = 0;
  for (uint16_t c : cv.px)
    sum += lin((c >> 11) / 31.0) + lin(((c >> 5) & 63) / 63.0) + lin((c & 31) / 31.0);
  return sum / (3.0 * cv.w * cv.h);
}

}  // namespace espn
