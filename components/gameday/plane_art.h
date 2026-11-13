// Side-view plane pictures and type kinds; pure, for host tests.
#pragma once

#include <cstdint>
#include <cstring>

namespace flights {

enum class PlaneKind : uint8_t {
  NONE = 0,
  GENERIC,
  LIGHT,
  TWIN_PROP,
  TURBOPROP,
  BIZJET,
  REGIONAL,
  NARROW,
  WIDE,
  JUMBO,
  HELI,
  FIGHTER,
  MIL_CARGO,
};
static const int kPlaneKinds = 13;

// Nose right; 2 bits a pixel, low bits first.
struct PlaneSprite {
  uint8_t w, h;
  const uint8_t *bits;
};

// Pixel values: clear, body, tail, detail.
enum : uint8_t { kArtClear = 0, kArtBody = 1, kArtTail = 2, kArtDetail = 3 };
static const int kPlaneArtMaxW = 56, kPlaneArtMaxH = 15;

// nullptr for NONE.
const PlaneSprite *plane_sprite(PlaneKind k);
// ICAO type designator, then ADS-B category.
PlaneKind plane_kind(const char *type, const char *category);

inline uint8_t sprite_px(const PlaneSprite &s, int x, int y) {
  const int i = y * s.w + x;
  return (uint8_t) ((s.bits[i / 4] >> (2 * (i % 4))) & 3);
}

}  // namespace flights
