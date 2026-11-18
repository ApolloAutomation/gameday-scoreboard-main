// Apollo Pulse's start-up screen: Apollo's art, his blink, the layouts.
#include <cstdio>
#include <cstring>
#include <fstream>
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

// One frame of apollo.txt as legend slots
std::vector<std::vector<int>> art_rows(const std::string &frame) {
  std::ifstream in("../firmware/art/sprites/apollo.txt");
  std::string line, legend;
  std::vector<std::vector<int>> rows;
  bool mine = false;
  while (std::getline(in, line)) {
    if (line.rfind("@legend ", 0) == 0)
      legend = line.substr(8);
    else if (line.rfind("@frame ", 0) == 0)
      mine = line.rfind("@frame " + frame + " ", 0) == 0;
    else if (line.empty() || line[0] == '#')
      mine = mine && !line.empty();
    else if (mine) {
      std::vector<int> r;
      for (char c : line)
        r.push_back((int) legend.find(c));
      rows.push_back(r);
    }
  }
  return rows;
}

std::vector<std::vector<int>> decode(const RleSprite &s) {
  std::vector<std::vector<int>> rows(s.h, std::vector<int>(s.w));
  int i = 0;
  for (int k = 0; k < s.n; k++)
    for (int j = 0; j <= (s.runs[k] & 15); j++, i++)
      if (i < s.w * s.h)
        rows[i / s.w][i % s.w] = s.runs[k] >> 4;
  BCHECK(i == s.w * s.h, "runs cover %d of %d pixels", i, s.w * s.h);
  return rows;
}

std::vector<uint16_t> dog_px(bool blink, bool ledge) {
  std::vector<uint16_t> buf((size_t) kDogSize * kDogSize, 0x1234);
  Px565 p;
  p.px = buf.data();
  p.w = p.h = p.stride = kDogSize;
  p.clip = PxBox{0, 0, kDogSize, kDogSize};
  paint_apollo(p, blink, ledge);
  return buf;
}

int count(const std::vector<uint16_t> &px, uint16_t c) {
  int n = 0;
  for (uint16_t v : px)
    n += v == c;
  return n;
}

void test_art() {
  const RleSprite &face = kSprApollo[SPR_APOLLO_FACE];
  const RleSprite &blink = kSprApollo[SPR_APOLLO_BLINK];
  BCHECK(decode(face) == art_rows("apollo.face"), "the packed face is apollo.txt's");
  BCHECK(decode(blink) == art_rows("apollo.blink"), "the packed blink is apollo.txt's");
  BCHECK(face.w <= kDogSize && face.h < kDogLedgeY, "face %dx%d sits above the ledge", face.w, face.h);
  const PxBox fb{(int16_t) (kDogNoseX - face.ax), (int16_t) (kDogNoseY - face.ay), face.w, face.h};
  BCHECK(fb.x >= 0 && fb.y >= 0 && fb.x + fb.w <= kDogSize && fb.y + fb.h <= kDogLedgeY, "face inside the box");

  const std::vector<uint16_t> px = dog_px(false, true);
  auto at = [&](int x, int y) { return px[(size_t) y * kDogSize + x]; };
  const uint16_t *pal = apollo_palette();
  BCHECK(at(kDogNoseX, kDogNoseY) == pal[1], "the nose's top is black");
  BCHECK(count(px, pal[5]) == 2, "two eye glints: %d", count(px, pal[5]));
  BCHECK(count(px, pal[4]) > 40, "a red tongue: %d px", count(px, pal[4]));
  BCHECK(count(px, pal[3]) > 800, "mostly orange: %d px", count(px, pal[3]));
  BCHECK(count(px, 0x1234) == 0, "every pixel painted");
  for (int c : {0, kDogSize - 1})
    for (int r : {0, kDogSize - 1})
      BCHECK(at(c, r) == rgb565(kBrandNavy), "corner %d,%d navy", c, r);
  for (int x = 1; x < kDogSize - 1; x++)
    BCHECK(at(x, kDogLedgeY) == pal[1], "ledge at x %d", x);
  BCHECK(dog_px(false, false)[(size_t) kDogLedgeY * kDogSize + 1] == rgb565(kBrandNavy), "no ledge when asked");
}

void test_blink() {
  const std::vector<uint16_t> open = dog_px(false, true), shut = dog_px(true, true);
  const PxBox b = apollo_blink_box();
  int changed = 0;
  for (int y = 0; y < kDogSize; y++)
    for (int x = 0; x < kDogSize; x++) {
      const size_t i = (size_t) y * kDogSize + x;
      if (open[i] == shut[i])
        continue;
      changed++;
      BCHECK(x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h, "blink changes %d,%d outside its band", x, y);
    }
  BCHECK(changed > 10 && changed < 80, "a blink changes %d pixels", changed);
  BCHECK(count(shut, apollo_palette()[5]) == 0, "both eyes shut");
  // Repainting only the band restores the open face
  std::vector<uint16_t> buf = shut;
  Px565 p;
  p.px = buf.data();
  p.w = p.h = p.stride = kDogSize;
  p.clip = b;
  paint_apollo(p, false, true);
  BCHECK(buf == open, "the band alone reopens his eyes");
}

void test_timing() {
  BCHECK(dog_turn(0, 12345), "no start yet: the dog");
  BCHECK(dog_turn(1000, 1000) && dog_turn(1000, 4999), "opens on the dog for 4 s");
  BCHECK(!dog_turn(1000, 5000) && !dog_turn(1000, 8999), "then the words for 4 s");
  BCHECK(dog_turn(1000, 9000), "and round again");
  BCHECK(dog_turn(0xFFFFF800u, 0x00000100u) && !dog_turn(0xFFFFF800u, 0x00001000u), "survives the millis() wrap");

  for (uint32_t seed : {0u, 1u, 0x2F6A70u, 0xDEADBEEFu}) {
    bool was = false;
    uint32_t shut_at = 0, last_open_end = 0, last_start = 0;
    int blinks = 0, doubles = 0, starts = 0;
    for (uint32_t t = 0; t < 10 * 60 * 1000; t += 10) {
      const bool on = blink_on(t, seed);
      if (on && !was) {
        shut_at = t;
        const uint32_t gap = t - last_open_end;
        if (blinks > 0 && gap < 1000) {
          doubles++;
          BCHECK(gap == kBlinkShutMs, "the double's open spell %u ms", (unsigned) gap);
        } else {
          if (starts > 0)
            BCHECK(t - last_start >= 4200 && t - last_start <= 6000, "seed %u: %u ms between blinks",
                   (unsigned) seed, (unsigned) (t - last_start));
          last_start = t;
          starts++;
        }
        blinks++;
      }
      if (!on && was) {
        BCHECK(t - shut_at == kBlinkShutMs, "eyes shut %u ms", (unsigned) (t - shut_at));
        last_open_end = t;
      }
      was = on;
    }
    BCHECK(starts >= 100 && starts <= 143, "seed %u: %d blinks in 10 min", (unsigned) seed, starts);
    BCHECK(doubles * 3 >= starts - 3 && doubles * 3 <= starts + 3, "every third a double: %d of %d", doubles,
           starts);
  }
  // Neighbouring panels' blinks rarely start together
  int starts = 0, together = 0, worst = 0;
  for (uint32_t a = 0x2F6A70u; a < 0x2F6A70u + 20; a++) {
    int mine = 0, both = 0;
    bool was = false;
    for (uint32_t t = 0; t < 600000; t += 10) {
      const bool on = blink_on(t, a);
      if (on && !was) {
        mine++;
        both += blink_on(t, a + 1);
      }
      was = on;
    }
    starts += mine;
    together += both;
    worst = both * 100 / mine > worst ? both * 100 / mine : worst;
  }
  BCHECK(together * 20 < starts, "%d of %d blinks start with the next panel's", together, starts);
  BCHECK(worst < 25, "at worst %d%% of one pair's blinks together", worst);
}

struct Size {
  int w, h;
};
const Size kSizes[] = {{64, 64}, {128, 64}, {192, 64}, {256, 64}, {128, 128}};

void test_boxes() {
  const PxBox none{};
  for (const Size &s : kSizes)
    for (int qr = 0; qr < 2; qr++)
      for (int dog = 0; dog < 2; dog++) {
        const BootBoxes b = boot_boxes(s.w, s.h, qr, dog);
        const PxBox scr{0, 0, (int16_t) s.w, (int16_t) s.h};
        const PxBox words = b.mode == BootWords::NONE ? none : PxBox{b.x0, b.y0, b.cw, 64};
        const char *tag = "";
        char buf[64];
        snprintf(buf, sizeof buf, "%dx%d qr %d dog %d", s.w, s.h, qr, dog);
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
        const int name = b.mode == BootWords::WIDE ? 12 * 8 : b.mode == BootWords::BESIDE ? 6 * 8 : 12 * 5;
        if (b.mode != BootWords::NONE) {
          BCHECK(name <= b.cw, "%s: APOLLO PULSE %d px in %d", tag, name, b.cw);
          BCHECK(12 * 5 <= b.cw - 4, "%s: Apollo Pulse and reconnecting fit", tag);
          BCHECK(b.bar.w >= 50 && b.bar.y > b.y0 + 9, "%s: the bar under the name", tag);
          for (int i = 0; i < 4; i++) {
            const int y = boot_line_y(b.mode, i);
            BCHECK(y < 0 || b.y0 + y + 8 <= s.h, "%s: line %d inside", tag, i);
            BCHECK(y < 0 || b.bar.y + b.bar.h <= b.y0 + y, "%s: line %d under the bar", tag, i);
          }
        } else {
          BCHECK(s.w < 128 && dog && !qr, "%s: words only give way on one 64x64", tag);
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
      for (int dog = 0; dog < 4; dog++)
        for (int blink = 0; blink < 2; blink++) {
          BootShot shot;
          shot.w = s.w;
          shot.h = s.h;
          memcpy(shot.lines, busy, sizeof busy);
          shot.qr = qr;
          shot.dog = dog;
          shot.blink = blink;
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
  test_blink();
  test_timing();
  test_boxes();
  test_screens();
  total_checks += checks;
  printf("boot brand: %d checks, %d failures\n", checks, failures);
  return failures;
}
