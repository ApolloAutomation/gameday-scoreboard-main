// Map landmarks under the radars; pure.
#pragma once

#include <cstddef>
#include <cstdint>

#include "apps.h"
#include "px565.h"

namespace espn {

// The device page's set, little endian, in bytes:
// header 16, then places 16, lines 2, points 4.
constexpr size_t kLandHead = 16, kLandPlaceBytes = 16;
constexpr size_t kLandMaxPlaces = 64, kLandMaxLines = 200, kLandMaxPoints = 1600;
constexpr size_t kLandMaxBytes = 8192;
constexpr int kLandMaxReach = 300;
// Offsets in 1/4000 degree.
constexpr int kLandMaxOff = 32000;
constexpr int kLandName = 10;
constexpr size_t kLandWorst = kLandHead + kLandMaxPlaces * kLandPlaceBytes + kLandMaxLines * 2 + kLandMaxPoints * 4;
static_assert(kLandWorst == 7840 && kLandWorst <= kLandMaxBytes, "the largest set fits the cap");

enum class LandKind : uint8_t { BORDER = 0, COAST = 1, ROAD = 2 };

enum class LandError : uint8_t { OK = 0, FORMAT, COUNTS, LENGTH, PLACE, NAME, LINE, POINT };
// The word /gameday/landmarks answers with.
const char *land_error_word(LandError e);

// Bytes land_check proved sound, and their header.
struct LandSet {
  const uint8_t *p{nullptr};
  size_t n{0};
  int16_t lat_e2{0}, lon_e2{0};
  uint16_t np{0}, nl{0}, npt{0}, reach_km{0};
  uint32_t id{0};  // the component's count of sets
};

struct LandPlace {
  int16_t dlat{0}, dlon{0};
  uint8_t cls{0}, len{0};
  char name[kLandName + 1]{};
};

LandError land_check(const uint8_t *p, size_t n, LandSet &out);
// Largest offsets a set's places and points may have.
void land_reach_box(int lat_e2, int reach_km, int &dlat, int &dlon);
// Degrees times 10,000 to times 100, halves away from zero.
int16_t land_e2(int32_t e4);
// A packed home for the web task; kLandNoHome for none.
constexpr uint32_t kLandNoHome = 0x7FFF7FFFu;
inline uint32_t land_pack(int16_t lat_e2, int16_t lon_e2) {
  return ((uint32_t) (uint16_t) lat_e2 << 16) | (uint16_t) lon_e2;
}
void land_place(const LandSet &s, size_t i, LandPlace &out);
// Line i's kind and point count; k its first point.
void land_line(const LandSet &s, size_t i, uint8_t &kind, uint8_t &n);
void land_point(const LandSet &s, size_t k, int16_t &dlat, int16_t &dlon);

// Zoom-7 Web Mercator, as the radar images use.
struct MapProj {
  float wx{0}, wy{0};  // the middle, in zoom-7 world pixels
  float s{1};          // canvas pixels per world pixel
  int16_t ox{0}, oy{0};
};
MapProj map_proj(int32_t lat_e4, int32_t lon_e4, float s, int w, int h);
// False when far outside the canvas.
bool map_xy(const MapProj &p, float lat, float lon, int &x, int &y);
// MapCanvas's scale for a 256 px tile.
float map_scale_weather(int w, int h);
// ring_px pixels for radius_m ground meters at lat.
float map_scale_ring(int32_t lat_e4, int ring_px, float radius_m);
// The smallest place class drawn at this scale.
int map_min_class(const AppMap &m);

struct MapLabel {
  int16_t x{0}, y{0};    // the place's dot
  int16_t lx{0}, ly{0};  // the label's top left
  uint8_t place{0};
};
constexpr int kMapLabelMax = 12;
// Most labels a canvas this size takes.
int map_label_cap(const AppMap &m);
// Places that fit with their label, biggest first.
int map_labels(const AppMap &m, const LandSet &s, MapLabel *out, int cap);
// The set drawn here: same rounded middle.
bool map_has(const AppMap &m, const LandSet *s);
// A radar plane's number or tag; w 0: none fits.
struct MapMark {
  int16_t x{0}, y{0}, w{0};
};
void map_marks(const AppMap &m, MapMark out[kAppBlips]);
// Black, lines, dots, labels; a radar's marks on top.
void map_paint(Px565 &p, const AppMap &m, const LandSet *set);

}  // namespace espn
