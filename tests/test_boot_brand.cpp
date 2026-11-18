// Apollo Pulse's start-up screen: Apollo's logo, the layouts.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "boot_brand.h"
#include "boot_render.h"

using namespace espn;
using namespace esphome::gameday;

namespace {

int checks = 0;
int failures = 0;

#define BCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

std::vector<uint16_t> dog_px() {
  std::vector<uint16_t> buf((size_t) kDogSize * kDogSize, 0x1234);
  Px565 p;
  p.px = buf.data();
  p.w = p.h = p.stride = kDogSize;
  p.clip = PxBox{0, 0, kDogSize, kDogSize};
  paint_apollo(p);
  return buf;
}

int r8(uint16_t c) { return (c >> 11) * 255 / 31; }
int g8(uint16_t c) { return ((c >> 5) & 63) * 255 / 63; }
int b8(uint16_t c) { return (c & 31) * 255 / 31; }
bool near(uint16_t c, uint32_t rgb, int tol) {
  return abs(r8(c) - (int) (rgb >> 16)) <= tol && abs(g8(c) - (int) ((rgb >> 8) & 255)) <= tol &&
         abs(b8(c) - (int) (rgb & 255)) <= tol;
}

void test_art() {
  BCHECK(kApolloNavy == kBrandNavy, "apollo_logo.h laid on today's navy: rerun build_apollo.py");
  BCHECK(kApolloH == kDogSize && kApolloW > 40 && kApolloW <= kDogSize, "Apollo %dx%d in his box", kApolloW,
         kApolloH);
  const int n = (int) (sizeof kApolloPal / sizeof kApolloPal[0]);
  for (uint8_t i : kApolloPx)
    BCHECK(i < n, "index %d past the palette", i);

  const std::vector<uint16_t> px = dog_px();
  auto at = [&](int x, int y) { return px[(size_t) y * kDogSize + x]; };
  const uint16_t navy = rgb565(kBrandNavy);
  BCHECK(std::count(px.begin(), px.end(), (uint16_t) 0x1234) == 0, "every pixel painted");
  for (int c : {0, kDogSize - 1})
    for (int r : {0, kDogSize - 1})
      BCHECK(at(c, r) == navy, "corner %d,%d navy", c, r);
  for (int y = 0; y < kDogSize; y++)
    for (int x = 0; x < (kDogSize - kApolloW) / 2; x++)
      BCHECK(at(x, y) == navy && at(kDogSize - 1 - x, y) == navy, "navy beside him at row %d", y);

  // The logo's own colors
  int orange = 0, charcoal = 0, tongue = 0, ink = 0;
  for (uint16_t c : px) {
    orange += near(c, 0xD47C3D, 12);
    charcoal += near(c, 0x3D3D3B, 12);
    tongue += near(c, 0xAE4144, 12);
    ink += near(c, 0x0F0E0C, 12);
  }
  BCHECK(orange > 400 && charcoal > 250 && tongue > 60 && ink > 300, "orange %d charcoal %d tongue %d ink %d", orange,
         charcoal, tongue, ink);

  // An eye's glint each side, in the upper half
  int glint_l = 0, glint_r = 0;
  for (int y = 16; y < 40; y++)
    for (int x = 0; x < kDogSize; x++) {
      const uint16_t c = at(x, y);
      if (r8(c) > 140 && g8(c) > 140 && b8(c) > 140)
        (x < kDogSize / 2 ? glint_l : glint_r)++;
    }
  BCHECK(glint_l >= 1 && glint_r >= 1, "glints left %d right %d", glint_l, glint_r);

  // No light halo: what touches navy is navy or darker
  const int navy_sum = r8(navy) + g8(navy) + b8(navy);
  for (int y = 0; y < kDogSize; y++)
    for (int x = 0; x < kDogSize; x++) {
      const uint16_t c = at(x, y);
      if (c == navy)
        continue;
      bool edge = false;
      for (int d = 0; d < 4; d++) {
        const int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
        edge |= nx >= 0 && ny >= 0 && nx < kDogSize && ny < kDogSize && at(nx, ny) == navy;
      }
      if (edge)
        BCHECK(r8(c) + g8(c) + b8(c) <= navy_sum + 24, "light edge at %d,%d", x, y);
    }

  // His ledge: the bar covers it on one panel
  int ledge = 0;
  for (int x = 0; x < kDogSize; x++)
    ledge += r8(at(x, kApolloLedgeY)) + g8(at(x, kApolloLedgeY)) + b8(at(x, kApolloLedgeY)) < navy_sum;
  BCHECK(ledge * 10 >= kApolloW * 9, "the ledge row %d holds %d dark px", kApolloLedgeY, ledge);
  const BootBoxes one = boot_boxes(64, 64, false, true);
  BCHECK(one.bar.y <= kApolloLedgeY && kApolloLedgeY < one.bar.y + one.bar.h && one.bar.y + one.bar.h == kDogSize,
         "one panel's bar %d covers the ledge %d", one.bar.y, kApolloLedgeY);
  BCHECK(one.bar.x <= (kDogSize - kApolloW) / 2 && one.bar.x + one.bar.w >= (kDogSize + kApolloW) / 2,
         "the bar spans the ledge");
}

void test_timing() {
  BCHECK(dog_turn(0, 12345), "no start yet: the dog");
  BCHECK(dog_turn(1000, 1000) && dog_turn(1000, 4999), "opens on the dog for 4 s");
  BCHECK(!dog_turn(1000, 5000) && !dog_turn(1000, 8999), "then the words for 4 s");
  BCHECK(dog_turn(1000, 9000), "and round again");
  BCHECK(dog_turn(0xFFFFF800u, 0x00000100u) && !dog_turn(0xFFFFF800u, 0x00001000u), "survives the millis() wrap");

}

struct Size {
  int w, h;
};
const Size kSizes[] = {{64, 64}, {128, 64}, {192, 64}, {256, 64}, {128, 128}};

void test_boxes() {
  const PxBox none{};
  for (const Size &s : kSizes)
    for (int qr = 0; qr < 2; qr++)
      for (int dog = 0; dog < 4; dog++) {
        const bool tall = dog & 2;
        const BootBoxes b = boot_boxes(s.w, s.h, qr, dog & 1, tall);
        const PxBox scr{0, 0, (int16_t) s.w, (int16_t) s.h};
        const PxBox words = b.mode == BootWords::NONE ? none : PxBox{b.x0, b.y0, b.cw, 64};
        const char *tag = "";
        char buf[64];
        snprintf(buf, sizeof buf, "%dx%d qr %d dog %d tall %d", s.w, s.h, qr, dog & 1, tall);
        tag = buf;
        for (const PxBox &x : {b.dog, b.qr, words, b.bar})
          BCHECK(x.empty() || box_clip(x, scr).w == x.w && box_clip(x, scr).h == x.h, "%s: a box off screen", tag);
        BCHECK(!box_overlap(b.dog, b.qr), "%s: dog and code overlap", tag);
        if (s.w >= 128) {
          BCHECK(!box_overlap(b.dog, words) && !box_overlap(b.qr, words), "%s: words under a box", tag);
          BCHECK(b.cw >= 64, "%s: words %d px", tag, b.cw);
        }
        BCHECK(qr == !b.qr.empty(), "%s: the code shows when up", tag);
        if (qr)
          BCHECK(b.qr.w == 64 && b.qr.h == 64, "%s: the code's box 64x64", tag);
        // The words' longest lines fit their column
        const bool split = boot_title_split(b.mode, tall);
        const BoardFont tf = b.mode == BootWords::NARROW ? (tall ? BoardFont::S12 : BoardFont::S8) : BoardFont::S16;
        const int name = (split ? 6 : 12) * font_px(tf);
        if (b.mode != BootWords::NONE) {
          BCHECK(name <= b.cw, "%s: APOLLO PULSE %d px in %d", tag, name, b.cw);
          BCHECK(12 * 5 <= b.cw - 4, "%s: Apollo Pulse and reconnecting fit", tag);
          // Two clear rows each side of the bar
          const int ink = b.y0 + boot_title_y(b.mode, tall) + (split ? font_rows(tf) : 0) + font_ink_bottom(tf);
          BCHECK(b.bar.w >= 50 && b.bar.y >= ink + 3, "%s: the bar %d under the name's ink %d", tag, b.bar.y, ink);
          for (int i = 0; i < 4; i++) {
            const int y = boot_line_y(b.mode, i, tall);
            const BoardFont lf = i == 1 && b.mode == BootWords::WIDE ? BoardFont::S12 : BoardFont::S8;
            BCHECK(y < 0 || b.y0 + y + font_desc_bottom(lf) < s.h, "%s: line %d inside", tag, i);
            BCHECK(y < 0 || i > 0 || b.bar.y + b.bar.h + 2 <= b.y0 + y + font_ink_top(lf), "%s: line %d off the bar",
                   tag, i);
            BCHECK(i < 3 || !tall || b.mode != BootWords::NARROW || y < 0, "%s: tall holds three lines", tag);
          }
        } else {
          BCHECK(s.w < 128 && (dog & 1) && !qr, "%s: words only give way on one 64x64", tag);
        }
      }
  // Where Apollo goes
  BCHECK(boot_boxes(64, 64, false, true).mode == BootWords::NONE, "64: the dog's turn fills it");
  BCHECK(boot_boxes(64, 64, true, true).dog.empty(), "64: never with the code");
  BCHECK(boot_boxes(128, 64, false, true).mode == BootWords::BESIDE, "128: name beside him");
  BCHECK(boot_boxes(128, 64, true, true).dog.empty() && boot_boxes(128, 64, true, true).qr.x == 0,
         "128: the code takes his place");
  const BootBoxes w3 = boot_boxes(192, 64, true, true);
  BCHECK(w3.dog.x == 0 && w3.qr.x == 64 && w3.x0 == 128 && w3.mode == BootWords::NARROW, "192 with the code");
  BCHECK(boot_boxes(192, 64, false, true).mode == BootWords::WIDE, "192: wide words beside him");
  const BootBoxes w4 = boot_boxes(256, 64, true, true);
  BCHECK(w4.qr.x == 64 && w4.x0 == 128 && w4.cw == 128 && w4.mode == BootWords::WIDE, "256 with the code");
  const BootBoxes q = boot_boxes(128, 128, true, true);
  BCHECK(q.qr.x == 32 && q.qr.y == 0 && q.dog.empty() && q.y0 == 64, "2x2: the code on top");
  BCHECK(boot_boxes(128, 128, false, false).y0 == 32, "2x2 countdown centered");
  BCHECK(!boot_words_wide(64, 64) && !boot_words_wide(128, 64) && boot_words_wide(192, 64) &&
             boot_words_wide(128, 128),
         "wide words where 128 px stay beside him");
  BCHECK(!boot_words_wide(64, 64, 3) && boot_words_wide(128, 64, 3) && boot_words_wide(256, 64, 3),
         "Updating keeps its wide words on 128x64");
  // Four lines never sit beside him
  BCHECK(!boot_dog_wanted(128, 64, 3, false, 1, 1) && boot_dog_wanted(192, 64, 3, false, 1, 1),
         "the address keeps four lines");
  BCHECK(!boot_dog_wanted(64, 64, 2, false, 1, 1) && boot_dog_wanted(64, 64, 1, false, 1, 1) &&
             !boot_dog_wanted(64, 64, 1, true, 1, 1),
         "one 64x64: turns only on a busy screen with no code");
}

void test_screens() {
  const ReviewFonts fonts;
  const char *busy[4] = {"", "Looking up", "scores", ""};
  for (const Size &s : kSizes)
    for (int qr = 0; qr < 2; qr++)
      for (int dog = 0; dog < 4; dog++) {
        BootShot shot;
        shot.w = s.w;
        shot.h = s.h;
        memcpy(shot.lines, busy, sizeof busy);
        shot.qr = qr;
        shot.dog = dog;
        shot.pct = 100;
        const Canvas cv = boot_render(shot, fonts);
        const double d = boot_drive(cv);
        BCHECK(d < 0.15, "%dx%d qr %d dog %d: drives %.3f of full white", s.w, s.h, qr, dog, d);
        if (!qr)
          continue;
        // Black all round the 29 px code
        const BootBoxes b = boot_boxes(s.w, s.h, true, boot_dog_wanted(s.w, s.h, dog, true, 1, 1));
        int lit = 0;
        for (int y = b.qr.y; y < b.qr.y + 64; y++)
          for (int x = b.qr.x; x < b.qr.x + 64; x++) {
            const bool code = x >= b.qr.x + 17 && x < b.qr.x + 46 && y >= b.qr.y + 17 && y < b.qr.y + 46;
            const uint16_t c = cv.px[(size_t) y * cv.w + x];
            if (!code)
              BCHECK(c == 0, "%dx%d: quiet zone lit at %d,%d", s.w, s.h, x, y);
            lit += c != 0;
          }
        BCHECK(lit > 200, "%dx%d: the code is drawn", s.w, s.h);
      }
}

}  // namespace

int run_boot_brand_tests(int &total_checks) {
  test_art();
  test_timing();
  test_boxes();
  test_screens();
  total_checks += checks;
  printf("boot brand: %d checks, %d failures\n", checks, failures);
  return failures;
}
