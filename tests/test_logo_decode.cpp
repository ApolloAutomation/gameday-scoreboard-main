// The logo task's PNG steps: size checks, fit, painting, rests.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "logo_decode.h"
#include "logo_icon.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define LCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

// A PNG's first 33 bytes: signature, IHDR, CRC
std::vector<uint8_t> png_head(uint32_t w, uint32_t h) {
  std::vector<uint8_t> b = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
  for (uint32_t v : {w, h})
    for (int s = 24; s >= 0; s -= 8)
      b.push_back((uint8_t) (v >> s));
  b.insert(b.end(), {8, 6, 0, 0, 0, 0, 0, 0, 0});
  return b;
}

// runtime_image 2026.10's fixed 32x32 fit
void ref_fit(uint32_t w, uint32_t h, int box, int &fw, int &fh) {
  const float scale = std::min((float) box / w, (float) box / h);
  fw = (int) (w * scale);
  fh = (int) (h * scale);
}

// runtime_image's ImageDecoder::draw and draw_pixel, RGB565 with alpha
void ref_paint(std::vector<uint8_t> &buf, int bw, int bh, int src_w, int src_h, int x, int y, int w, int h,
               const uint8_t c[4]) {
  const double xs = (double) bw / src_w, ys = (double) bh / src_h;
  const int width = std::min(bw, (int) std::ceil((x + w) * xs));
  const int height = std::min(bh, (int) std::ceil((y + h) * ys));
  const uint16_t v = (uint16_t) ((c[0] * 32 / 256) << 11 | (c[1] * 64 / 256) << 5 | (c[2] * 32 / 256));
  for (int i = x * xs; i < width; i++)
    for (int j = y * ys; j < height; j++) {
      const size_t pos = (size_t) (i + j * bw) * 2;
      buf[pos] = (uint8_t) (v & 0xFF);
      buf[pos + 1] = (uint8_t) (v >> 8);
      buf[pos / 2 + (size_t) bw * bh * 2] = c[3];
    }
}

void color_at(uint32_t x, uint32_t y, uint8_t c[4]) {
  c[0] = (uint8_t) (x * 7 + y);
  c[1] = (uint8_t) (y * 5 + 3 * x);
  c[2] = (uint8_t) (x ^ y);
  c[3] = (uint8_t) ((x + 2 * y) & 0xFF);
}

// Paints every source pixel the way pngle hands them over.
void paint_both(uint32_t w, uint32_t h, bool interlaced, std::vector<uint8_t> &mine, std::vector<uint8_t> &ref,
                int &fw, int &fh) {
  logo_fit(w, h, 32, fw, fh);
  mine.assign(logo_icon_bytes(fw, fh), 0);
  ref.assign(logo_icon_bytes(fw, fh), 0);
  LogoCanvas canvas{mine.data(), w, h, fw, fh};
  uint8_t c[4];
  if (!interlaced) {
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++) {
        color_at(x, y, c);
        canvas.paint(x, y, 1, 1, c);
        ref_paint(ref, fw, fh, w, h, x, y, 1, 1, c);
      }
    return;
  }
  // Adam7, as pngle's pngle_draw_pixels draws its passes
  static const uint32_t off_x[8] = {0, 0, 4, 0, 2, 0, 1, 0}, off_y[8] = {0, 0, 0, 4, 0, 2, 0, 1};
  static const uint32_t div_x[8] = {1, 8, 8, 4, 4, 2, 2, 1}, div_y[8] = {1, 8, 8, 8, 4, 4, 2, 2};
  for (int p = 1; p <= 7; p++)
    for (uint32_t y = off_y[p]; y < h; y += div_y[p])
      for (uint32_t x = off_x[p]; x < w; x += div_x[p]) {
        const uint32_t rw = std::min(div_x[p] - off_x[p], w - x), rh = std::min(div_y[p] - off_y[p], h - y);
        color_at(x, y, c);
        canvas.paint(x, y, rw, rh, c);
        ref_paint(ref, fw, fh, w, h, x, y, rw, rh, c);
      }
}

void test_png_size() {
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> b = png_head(500, 478);
  LCHECK(png_size(b.data(), b.size(), w, h) && w == 500 && h == 478, "ECHL 71's 500x478 read as %ux%u", w, h);
  LCHECK(!png_size(b.data(), 23, w, h), "too short for the IHDR");
  LCHECK(!png_size(nullptr, 33, w, h), "no body");
  b = png_head(500, 478);
  b[1] = 'Q';
  LCHECK(!png_size(b.data(), b.size(), w, h), "not a PNG signature");
  b = png_head(500, 478);
  b[11] = 14;
  LCHECK(!png_size(b.data(), b.size(), w, h), "an IHDR of the wrong length");
  b = png_head(500, 478);
  b[12] = 'i';
  LCHECK(!png_size(b.data(), b.size(), w, h), "a first chunk that isn't IHDR");
  b = png_head(0, 50);
  LCHECK(!png_size(b.data(), b.size(), w, h), "zero wide");
  const char html[] = "<!DOCTYPE html><html><body>Not found</body></html>";
  LCHECK(!png_size((const uint8_t *) html, sizeof html, w, h), "an error page");
}

void test_size_ok() {
  LCHECK(logo_size_ok(500, 478), "the 500 px HockeyTech logos decode");
  LCHECK(logo_size_ok(500, 500), "Lehigh Valley's 500x500");
  LCHECK(logo_size_ok(64, 64) && logo_size_ok(50, 50) && logo_size_ok(250, 250), "ESPN, MLB, market sizes");
  LCHECK(logo_size_ok(1024, 1024) && logo_size_ok(4096, 256), "a megapixel is the limit");
  LCHECK(!logo_size_ok(1025, 1024), "just past a megapixel");
  LCHECK(logo_size_ok(1, kLogoMaxPixels) && !logo_size_ok(1, kLogoMaxPixels + 1), "a one pixel line");
  LCHECK(!logo_size_ok(70000, 70000), "no overflow on huge sizes");
  LCHECK(!logo_size_ok(0, 5) && !logo_size_ok(5, 0), "empty");
}

void test_fit() {
  int fw = 0, fh = 0;
  logo_fit(500, 478, 32, fw, fh);
  LCHECK(fw == 32 && fh == 30, "500x478 fits as 32x30, got %dx%d", fw, fh);
  logo_fit(478, 500, 32, fw, fh);
  LCHECK(fw == 30 && fh == 32, "478x500 fits as 30x32, got %dx%d", fw, fh);
  logo_fit(16, 16, 32, fw, fh);
  LCHECK(fw == 32 && fh == 32, "small logos grow to the box");
  logo_fit(1000, 3, 32, fw, fh);
  LCHECK(fw == 32 && fh == 1, "a thin banner keeps a row, got %dx%d", fw, fh);
  // The sizes logos come in fit as runtime_image fitted them.
  const uint32_t sizes[][2] = {{500, 478}, {500, 500}, {64, 64}, {50, 50}, {32, 32}, {100, 100},
                               {250, 250}, {200, 100}, {100, 37}, {37, 100}, {60, 45}};
  for (const auto &s : sizes) {
    int rw = 0, rh = 0;
    logo_fit(s[0], s[1], 32, fw, fh);
    ref_fit(s[0], s[1], 32, rw, rh);
    LCHECK(fw == rw && fh == rh, "%ux%u fits as %dx%d, runtime_image %dx%d", s[0], s[1], fw, fh, rw, rh);
  }
}

void test_paint() {
  // RGB565 little endian, then the alpha plane.
  std::vector<uint8_t> px(logo_icon_bytes(32, 32), 0);
  LogoCanvas canvas{px.data(), 32, 32, 32, 32};
  const uint8_t red[4] = {255, 0, 0, 128}, green[4] = {0, 255, 0, 255};
  canvas.paint(0, 0, 1, 1, red);
  canvas.paint(1, 0, 1, 1, green);
  LCHECK(px[0] == 0x00 && px[1] == 0xF8 && px[2 * 1024] == 128, "red %02x %02x alpha %u", px[0], px[1], px[2048]);
  LCHECK(px[2] == 0xE0 && px[3] == 0x07 && px[2 * 1024 + 1] == 255, "green %02x %02x", px[2], px[3]);
  canvas.paint(32, 0, 1, 1, red);
  canvas.paint(0, 40, 1, 1, red);
  LogoCanvas none;
  none.paint(0, 0, 1, 1, red);
  LCHECK(px[4] == 0 && px[2 * 1024 + 2] == 0, "paints off the source or with no canvas do nothing");

  // Pixel for pixel what runtime_image painted, plain and interlaced.
  const uint32_t sizes[][2] = {{500, 478}, {500, 500}, {64, 64}, {50, 50}, {16, 16}, {100, 37}, {37, 100}, {33, 31}};
  for (const auto &s : sizes)
    for (bool interlaced : {false, true}) {
      std::vector<uint8_t> mine, ref;
      int fw = 0, fh = 0;
      paint_both(s[0], s[1], interlaced, mine, ref, fw, fh);
      size_t diff = 0;
      for (size_t i = 0; i < mine.size(); i++)
        diff += mine[i] != ref[i];
      LCHECK(diff == 0, "%ux%u%s to %dx%d: %zu bytes differ from runtime_image", s[0], s[1],
             interlaced ? " interlaced" : "", fw, fh, diff);
    }

  // Icon path: a solid 500x478 logo to 7 px
  int fw = 0, fh = 0;
  logo_fit(500, 478, 32, fw, fh);
  std::vector<uint8_t> logo(logo_icon_bytes(fw, fh), 0);
  LogoCanvas big{logo.data(), 500, 478, fw, fh};
  const uint8_t navy[4] = {20, 40, 200, 255};
  for (uint32_t y = 0; y < 478; y++)
    for (uint32_t x = 0; x < 500; x++)
      big.paint(x, y, 1, 1, navy);
  const std::vector<uint8_t> icon = logo_icon_shrink(logo.data(), fw, fh, 7);
  int r, g, b, a;
  logo_px_get(icon.data(), 49, 24, r, g, b, a);
  LCHECK(icon.size() == logo_icon_bytes(7, 7) && b > 180 && r < 40 && a == 255, "icon %d %d %d %d", r, g, b, a);
}

void test_rest() {
  LCHECK(!logo_rest_due(100, 100 + kLogoSliceMs - 1), "keeps decoding inside a slice");
  LCHECK(logo_rest_due(100, 100 + kLogoSliceMs), "rests once a slice is up");
  LCHECK(logo_rest_due(0xFFFFFFFAu, 0xFFFFFFFAu + kLogoSliceMs), "across the millis() wrap");
  LCHECK(!logo_rest_due(0xFFFFFFFAu, 3), "a short slice across the wrap");
  LCHECK(kLogoSliceMs <= 30, "a slice is shorter than a loop pass's budget");
}

}  // namespace

int run_logo_decode_tests(int &total_checks) {
  test_png_size();
  test_size_ok();
  test_fit();
  test_paint();
  test_rest();
  total_checks += checks;
  return failures;
}
