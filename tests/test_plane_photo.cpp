// The card's photo steps: the cut, scaling and lighting.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "plane_photo.h"

using namespace flights;

namespace {

int checks = 0;
int failures = 0;

#define PPCHECK(cond, ...)                        \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

uint16_t rgb565(int r, int g, int b) { return (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

void test_fit() {
  // Planespotters' 420x280, progressive: its first scan at 1/8
  PhotoFit f = photo_fit(420, 280, true, 56, 21);
  PPCHECK(f.ok() && f.shift == 3 && f.sw == 53 && f.sh == 35, "decoded %dx%d at 1/%d", f.sw, f.sh, 1 << f.shift);
  PPCHECK(f.cx == 0 && f.cw == 53 && f.ch == 21 && f.cy == 2, "the whole width, 60%% of the height, mostly off "
          "the bottom: %d,%d %dx%d", f.cx, f.cy, f.cw, f.ch);
  PPCHECK(f.ow == 53 && f.oh == 21, "drawn as decoded: %dx%d", f.ow, f.oh);
  f = photo_fit(448, 280, true, 56, 21);
  PPCHECK(f.sw == 56 && f.ow == 56 && f.oh == 21, "a wider one fills the box: %dx%d", f.ow, f.oh);
  // A small progressive one: never drawn bigger than decoded
  f = photo_fit(200, 133, true, 56, 21);
  PPCHECK(f.sw == 25 && f.sh == 17 && f.ow == 25 && f.oh <= f.ch && f.oh == 11, "the thumbnail: %dx%d", f.ow, f.oh);
  // Baseline: the smallest decode still as big as drawn
  f = photo_fit(200, 133, false, 56, 21);
  PPCHECK(f.shift == 1 && f.sw == 100 && f.ow <= f.cw && f.oh <= f.ch && f.oh == 21, "baseline 200: 1/%d, %dx%d",
          1 << f.shift, f.ow, f.oh);
  f = photo_fit(1024, 768, false, 56, 21);
  PPCHECK(f.shift == 3 && f.oh == 21, "baseline 1024: 1/%d", 1 << f.shift);
  f = photo_fit(64, 40, false, 56, 21);
  PPCHECK(f.shift == 0 && f.ow <= 56 && f.oh == 21, "a tiny one at full size: %dx%d", f.ow, f.oh);
  // A panorama fills the width
  f = photo_fit(1000, 200, false, 56, 21);
  PPCHECK(f.ow == 56 && f.oh < 21 && f.ch == f.sh, "a panorama: %dx%d", f.ow, f.oh);
  // Full screen's box: the first scan grown to fill it
  f = photo_fit(420, 280, true, 126, 52);
  PPCHECK(f.sw == 53 && f.cw == 53 && f.ch == 22 && f.ow == 125 && f.oh == 52, "grown: cut %dx%d, drawn %dx%d", f.cw,
          f.ch, f.ow, f.oh);
  f = photo_fit(448, 280, true, 126, 52);
  PPCHECK(f.ow == 121 && f.oh == 52, "a wider one grown: %dx%d", f.ow, f.oh);
  // Baseline decodes big enough instead
  f = photo_fit(1024, 683, false, 126, 52);
  PPCHECK(f.shift == 3 && f.ow == 126 && f.oh <= 52 && f.ow <= f.cw, "baseline 1024 at full screen: %dx%d", f.ow, f.oh);
  // Sizes it refuses outright
  PPCHECK(!photo_fit(0, 280, true, 56, 21).ok() && !photo_fit(420, 0, true, 56, 21).ok(), "empty");
  PPCHECK(!photo_fit(1025, 1024, false, 56, 21).ok() && photo_fit(1024, 1024, false, 56, 21).ok(), "the 1 MP cap");
  PPCHECK(!photo_fit(420, 280, true, 0, 21).ok() && !photo_fit(420, 280, true, 56, 0).ok(), "no box");
  // Every size keeps inside its box and its picture
  int bad = 0;
  for (uint32_t w = 1; w <= 1100; w += 37)
    for (uint32_t h = 1; h <= 1100; h += 41)
      for (int prog = 0; prog < 2; prog++)
        for (int bw : {56, 21, 64, 126})
          for (int bh : {21, 32, 52, 56}) {
            const PhotoFit g = photo_fit(w, h, prog, bw, bh);
            if ((uint64_t) w * h > kPhotoMaxPixels) {
              bad += g.ok();
              continue;
            }
            // Never bigger than cut, unless grown to fill the box
            const bool grown = 2 * g.cw <= bw && 2 * g.ch <= bh && (g.ow == bw || g.oh == bh);
            const bool in = g.ok() && g.ow <= bw && g.oh <= bh && ((g.ow <= g.cw && g.oh <= g.ch) || grown) &&
                            g.cw == g.sw && g.cy >= 0 && g.cy + g.ch <= g.sh &&
                            g.sw == (int) ((w + (1u << g.shift) - 1) >> g.shift);
            // The shape kept to a pixel
            const bool shape = std::abs((int64_t) g.ow * g.ch - (int64_t) g.oh * g.cw) <= std::max(g.cw, g.ch);
            if (!in || !shape) {
              if (bad++ < 5)
                printf("  %ux%u%s in %dx%d: cut %d,%d %dx%d of %dx%d, drawn %dx%d\n", w, h, prog ? " progressive" : "",
                       bw, bh, g.cx, g.cy, g.cw, g.ch, g.sw, g.sh, g.ow, g.oh);
            }
          }
  PPCHECK(bad == 0, "%d sizes out of their box or shape", bad);
}

void test_scale() {
  PhotoFit f = photo_fit(420, 280, true, 56, 21);
  std::vector<uint16_t> px((size_t) f.sw * f.sh, rgb565(40, 120, 200));
  std::vector<uint8_t> rgb((size_t) f.ow * f.oh * 3);
  photo_scale(px.data(), f, rgb.data());
  const PhotoRgb c = photo_rgb(rgb565(40, 120, 200));
  bool flat = true;
  for (size_t i = 0; i < rgb.size(); i += 3)
    flat = flat && rgb[i] == c.r && rgb[i + 1] == c.g && rgb[i + 2] == c.b;
  PPCHECK(flat, "one color stays that color");
  // Halving a checkerboard averages it
  PhotoFit h;
  h.sw = h.cw = 8;
  h.sh = h.ch = 4;
  h.ow = 4;
  h.oh = 2;
  std::vector<uint16_t> board(32);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 8; x++)
      board[(size_t) y * 8 + x] = (x + y) % 2 ? rgb565(255, 255, 255) : 0;
  std::vector<uint8_t> half(4 * 2 * 3);
  photo_scale(board.data(), h, half.data());
  bool grey = true;
  for (uint8_t v : half)
    grey = grey && (v == 127 || v == 128);
  PPCHECK(grey, "a checkerboard halves to grey: %u", half[0]);
  // Only the cut's rows: the red above stays out
  PhotoFit cut = h;
  cut.cy = 2;
  cut.ch = 2;
  cut.oh = 1;
  for (int x = 0; x < 8; x++) {
    board[(size_t) x] = board[(size_t) 8 + x] = rgb565(255, 0, 0);
    board[(size_t) 16 + x] = board[(size_t) 24 + x] = rgb565(0, 0, 255);
  }
  std::vector<uint8_t> low(4 * 3);
  photo_scale(board.data(), cut, low.data());
  PPCHECK(low[0] == 0 && low[2] == 255, "only the cut's rows: %u %u %u", low[0], low[1], low[2]);
}

void test_light() {
  // Sky at both ends, the plane in the middle
  const int w = 8, h = 2;
  std::vector<uint8_t> rgb((size_t) w * h * 3);
  for (int i = 0; i < w * h; i++) {
    const bool plane = i % w >= 3 && i % w <= 4;
    rgb[(size_t) i * 3] = plane ? 250 : 120;
    rgb[(size_t) i * 3 + 1] = plane ? 250 : 170;
    rgb[(size_t) i * 3 + 2] = plane ? 250 : 230;
  }
  std::vector<uint8_t> out((size_t) w * h * 2);
  photo_light(rgb.data(), w, h, out.data());
  auto at = [&](int i) { return photo_rgb((uint16_t) (out[(size_t) 2 * i] | out[(size_t) 2 * i + 1] << 8)); };
  const PhotoRgb sky = at(0), plane = at(3);
  PPCHECK(sky.b <= 230 * kPhotoDimLo / 256 + 8 && sky.b >= 230 * kPhotoDimLo / 256 - 8, "the sky dims: %d", sky.b);
  PPCHECK(plane.r >= 240 && plane.g >= 240 && plane.b >= 240, "the plane stays lit: %d %d %d", plane.r, plane.g,
          plane.b);
  // Low byte first, as LVGL reads RGB565
  std::vector<uint8_t> one = {255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0};
  std::vector<uint8_t> red(8);
  photo_light(one.data(), 4, 1, red.data());
  PPCHECK(red[0] == 0x00 && red[1] == ((255 * kPhotoDimLo >> 8) & 0xF8), "RGB565 order: %02x %02x", red[0], red[1]);
  // Too narrow to tell the sky: left as it is
  std::vector<uint8_t> narrow = {10, 20, 30, 200, 210, 220};
  std::vector<uint8_t> n2(4);
  photo_light(narrow.data(), 2, 1, n2.data());
  const PhotoRgb a = photo_rgb((uint16_t) (n2[0] | n2[1] << 8)), b = photo_rgb((uint16_t) (n2[2] | n2[3] << 8));
  PPCHECK(std::abs(a.r - 10) <= 8 && std::abs(b.b - 220) <= 8, "narrow: as is");
}

}  // namespace

int run_plane_photo_tests(int &total_checks) {
  test_fit();
  test_scale();
  test_light();
  total_checks += checks;
  return failures;
}
