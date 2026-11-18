// Start-up screen layouts and Apollo; no ESPHome includes
#pragma once

#include <cstdint>

#include "apollo_logo.h"
#include "px565.h"

namespace esphome::gameday {

// Brand colors, 0xRRGGBB
// The start-up screens' background
static const uint32_t kBootBg = 0x000000;
static const uint32_t kBrandBlue = 0x4379AA;
static const uint32_t kBrandGreen = 0x9ABD32;
static const uint32_t kBrandOrange = 0xD57D3B;
static const uint32_t kBrandLight = 0xD4E8F8;

static const int kDogSize = 64;
// His ledge's rows: where one panel's bar sits
static const int kDogBarY = kDogSize - 3;

// Black, then Apollo centered in his 64x64 box
inline void paint_apollo(::espn::Px565 &p) {
  p.fill(0, 0, kDogSize, kDogSize, ::espn::rgb565(kBootBg));
  const int x0 = (kDogSize - ::espn::kApolloW) / 2, y0 = kDogSize - ::espn::kApolloH;
  for (int y = 0; y < ::espn::kApolloH; y++)
    for (int x = 0; x < ::espn::kApolloW; x++)
      p.put(x0 + x, y0 + y, ::espn::kApolloPal[::espn::kApolloPx[y * ::espn::kApolloW + x]]);
}

// Words: 64 px, 128 px or more, or beside Apollo
enum class BootWords : uint8_t { NONE, NARROW, WIDE, BESIDE };

struct BootBoxes {
  ::espn::PxBox dog, qr;
  int16_t x0{0}, cw{0}, y0{0};
  BootWords mode{BootWords::NONE};
  ::espn::PxBox bar;
  int16_t rule_y{0};
};

// dog: Apollo shown; tall: three lines in 64 px
inline BootBoxes boot_boxes(int W, int H, bool qr, bool dog, bool tall = false) {
  BootBoxes b;
  auto slot = [](int x, int y) { return ::espn::PxBox{(int16_t) x, (int16_t) y, kDogSize, kDogSize}; };
  if (W < 128) {
    if (qr)
      b.qr = slot(0, 0);
    if (dog && !qr) {
      b.dog = slot(0, 0);
      b.bar = ::espn::PxBox{4, kDogBarY, 56, 3};
      return b;
    }
    b.mode = BootWords::NARROW;
    b.cw = (int16_t) W;
  } else if (H > 64) {
    // 2x2: the dog or the code on top
    if (qr || dog) {
      (qr ? b.qr : b.dog) = slot((W - kDogSize) / 2, 0);
      b.y0 = kDogSize;
    } else {
      b.y0 = (int16_t) ((H - 64) / 2);
    }
    b.mode = BootWords::WIDE;
    b.cw = (int16_t) W;
  } else {
    if (qr && dog && W - 2 * kDogSize < 64)
      dog = false;
    int x = 0;
    if (dog) {
      b.dog = slot(x, 0);
      x += kDogSize;
    }
    if (qr) {
      b.qr = slot(x, 0);
      x += kDogSize;
    }
    b.x0 = (int16_t) x;
    b.cw = (int16_t) (W - x);
    b.mode = b.cw >= 128 ? BootWords::WIDE : (dog && !qr) ? BootWords::BESIDE : BootWords::NARROW;
  }
  const int x0 = b.x0, y0 = b.y0;
  switch (b.mode) {
    case BootWords::NARROW:
      b.bar = ::espn::PxBox{(int16_t) (x0 + 4), (int16_t) (y0 + (tall ? 25 : 11)), 56, 3};
      b.rule_y = (int16_t) (b.bar.y + 1);
      break;
    case BootWords::WIDE:
      b.bar = ::espn::PxBox{(int16_t) (x0 + 4), (int16_t) (y0 + 18), (int16_t) (b.cw - 8), 3};
      b.rule_y = (int16_t) (y0 + 19);
      break;
    case BootWords::BESIDE:
      b.bar = ::espn::PxBox{(int16_t) (x0 + 6), (int16_t) (y0 + 32), 52, 3};
      b.rule_y = (int16_t) (y0 + 33);
      break;
    default:
      break;
  }
  return b;
}

// Line tops for la..ld, relative to y0; -1 unused
inline int boot_line_y(BootWords m, int i, bool tall = false) {
  static const int8_t kY[4][4] = {{16, 26, 36, 46}, {31, 41, 51, -1}, {23, 32, 45, 54}, {37, 46, 55, -1}};
  return m == BootWords::NARROW ? kY[tall ? 1 : 0][i]
         : m == BootWords::WIDE ? kY[2][i]
         : m == BootWords::BESIDE ? kY[3][i]
                                  : -1;
}

// The name in two lines: beside Apollo, or tall
inline bool boot_title_split(BootWords m, bool tall) {
  return m == BootWords::BESIDE || (m == BootWords::NARROW && tall);
}

// Name top, relative to y0
inline int boot_title_y(BootWords m, bool tall = false) { return boot_title_split(m, tall) ? 1 : 2; }

// Busy states on one 64x64: the dog, then the words
static const uint32_t kDogTurnMs = 4000;
inline bool dog_turn(uint32_t since_ms, uint32_t now_ms) {
  return since_ms == 0 || ((uint32_t) (now_ms - since_ms) / kDogTurnMs) % 2 == 0;
}

// dog 0 none, 1 busy, 2 beside, 3 wide only
inline bool boot_dog_wanted(int W, int H, int dog, bool qr, uint32_t since_ms, uint32_t now_ms) {
  if (dog == 0)
    return false;
  if (W < 128)
    return dog == 1 && !qr && dog_turn(since_ms, now_ms);
  return dog != 3 || boot_boxes(W, H, qr, true).mode != BootWords::BESIDE;
}

// Words get 128 px or more for this request
inline bool boot_words_wide(int W, int H, int dog = 1) {
  return boot_boxes(W, H, false, boot_dog_wanted(W, H, dog, false, 0, 0)).cw >= 128;
}

}  // namespace esphome::gameday
