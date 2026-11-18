// Start-up screen layouts and Apollo; no ESPHome includes
#pragma once

#include <cstdint>

#include "px565.h"
#include "sprites.h"

namespace esphome::gameday {

// Brand colors, 0xRRGGBB
static const uint32_t kBrandNavy = 0x1F425F;
static const uint32_t kBrandBlue = 0x4379AA;
static const uint32_t kBrandGreen = 0x9ABD32;
static const uint32_t kBrandOrange = 0xD57D3B;
static const uint32_t kBrandLight = 0xD4E8F8;

// Apollo's palette, by apollo.txt's legend
inline const uint16_t *apollo_palette() {
  static const uint16_t kPal[16] = {0,
                                    ::espn::rgb565(0x000000),
                                    ::espn::rgb565(0x3D3D3B),
                                    ::espn::rgb565(kBrandOrange),
                                    ::espn::rgb565(0xB34845),
                                    ::espn::rgb565(0xFFFFFF)};
  return kPal;
}

static const int kDogSize = 64;
// Top of the nose in the dog's box
static const int kDogNoseX = 32, kDogNoseY = 34;
static const int kDogLedgeY = 59;

// The eye band the blink repaints, in the dog's box
inline ::espn::PxBox apollo_blink_box() {
  const ::espn::RleSprite &b = ::espn::kSprApollo[::espn::SPR_APOLLO_BLINK];
  return ::espn::PxBox{(int16_t) (kDogNoseX - b.ax), (int16_t) (kDogNoseY - b.ay), (int16_t) b.w, (int16_t) b.h};
}

// Navy, Apollo, shut eyes on blink, his ledge
inline void paint_apollo(::espn::Px565 &p, bool blink, bool ledge) {
  const uint16_t *pal = apollo_palette();
  p.fill(0, 0, kDogSize, kDogSize, ::espn::rgb565(kBrandNavy));
  ::espn::px_sprite_rle(p, ::espn::kSprApollo[::espn::SPR_APOLLO_FACE], kDogNoseX, kDogNoseY, pal, 1);
  if (blink)
    ::espn::px_sprite_rle(p, ::espn::kSprApollo[::espn::SPR_APOLLO_BLINK], kDogNoseX, kDogNoseY, pal, 1);
  if (ledge)
    p.hline(1, kDogLedgeY, kDogSize - 2, pal[1]);
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

// dog: Apollo wanted, and on one 64x64 it is his turn
inline BootBoxes boot_boxes(int W, int H, bool qr, bool dog) {
  BootBoxes b;
  auto slot = [](int x, int y) { return ::espn::PxBox{(int16_t) x, (int16_t) y, kDogSize, kDogSize}; };
  if (W < 128) {
    if (qr)
      b.qr = slot(0, 0);
    if (dog && !qr) {
      b.dog = slot(0, 0);
      b.bar = ::espn::PxBox{4, kDogLedgeY, 56, 3};
      return b;
    }
    b.mode = BootWords::NARROW;
    b.cw = (int16_t) W;
  } else if (H > 64) {
    // Two rows of two: the dog or the code on top
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
      b.bar = ::espn::PxBox{(int16_t) (x0 + 4), (int16_t) (y0 + 11), 56, 3};
      b.rule_y = (int16_t) (y0 + 12);
      break;
    case BootWords::WIDE:
      b.bar = ::espn::PxBox{(int16_t) (x0 + 4), (int16_t) (y0 + 19), (int16_t) (b.cw - 8), 3};
      b.rule_y = (int16_t) (y0 + 19);
      break;
    case BootWords::BESIDE:
      b.bar = ::espn::PxBox{(int16_t) (x0 + 6), (int16_t) (y0 + 33), 52, 3};
      b.rule_y = (int16_t) (y0 + 34);
      break;
    default:
      break;
  }
  return b;
}

// The words keep 128 px or more beside Apollo
inline bool boot_words_wide(int W, int H) { return boot_boxes(W, H, false, true).cw >= 128; }

// Line tops for la..ld, relative to y0; -1 unused
inline int boot_line_y(BootWords m, int i) {
  static const int8_t kY[3][4] = {{16, 26, 36, 46}, {22, 31, 44, 53}, {37, 46, 55, -1}};
  return m == BootWords::NARROW ? kY[0][i] : m == BootWords::WIDE ? kY[1][i] : m == BootWords::BESIDE ? kY[2][i] : -1;
}

// Name top, relative to y0
inline int boot_title_y(BootWords m) { return m == BootWords::BESIDE ? 1 : 2; }

// Busy states on one 64x64: the dog, then the words
static const uint32_t kDogTurnMs = 4000;
inline bool dog_turn(uint32_t since_ms, uint32_t now_ms) {
  return since_ms == 0 || ((uint32_t) (now_ms - since_ms) / kDogTurnMs) % 2 == 0;
}

// dog: 0 none, 1 busy, 2 beside words, 3 not BESIDE
inline bool boot_dog_wanted(int W, int H, int dog, bool qr, uint32_t since_ms, uint32_t now_ms) {
  if (dog == 0)
    return false;
  if (W < 128)
    return dog == 1 && !qr && dog_turn(since_ms, now_ms);
  return dog != 3 || boot_boxes(W, H, qr, true).mode != BootWords::BESIDE;
}

// One blink a 5.1 s slot: gaps 4.2 to 6 s
static const uint32_t kBlinkSlotMs = 5100, kBlinkJitterMs = 900, kBlinkShutMs = 120;
inline uint32_t blink_hash(uint32_t v) {
  v ^= v >> 16;
  v *= 0x7FEB352Du;
  v ^= v >> 15;
  v *= 0x846CA68Bu;
  v ^= v >> 16;
  return v;
}

// Eyes shut now; every third blink is a double
inline bool blink_on(uint32_t now_ms, uint32_t seed) {
  // Each panel's slots start at its own phase
  now_ms += blink_hash(seed) % kBlinkSlotMs;
  const uint32_t slot = now_ms / kBlinkSlotMs;
  const uint32_t at = blink_hash(slot ^ seed) % kBlinkJitterMs;
  const uint32_t t = now_ms % kBlinkSlotMs;
  if (t < at)
    return false;
  const uint32_t in = t - at;
  if ((slot + seed) % 3 == 2)
    return in < kBlinkShutMs || (in >= 2 * kBlinkShutMs && in < 3 * kBlinkShutMs);
  return in < kBlinkShutMs;
}

}  // namespace esphome::gameday
