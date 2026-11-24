// A drawn plane over a small scene; pure, for tests.
#pragma once

#include <cstdint>

#include "apps.h"
#include "plane_art.h"
#include "px565.h"

namespace espn {

// The sky's frame step, as its timer runs
constexpr uint32_t kSkyFrameMs = 100;
// Rotors and propellers turn a step this often
constexpr uint32_t kSkySpinMs = 250;

// Differs from plane to plane and every 10 minutes.
uint32_t sky_seed(const char *hex, int64_t now);
// The sun's middle above the horizon, refraction included.
bool sun_up(int32_t lat_e4, int32_t lon_e4, int64_t epoch);
// Its elevation in whole degrees; rising before its noon.
int sun_elev(int32_t lat_e4, int32_t lon_e4, int64_t epoch, bool &rising);
// A home's scenery seed, the same within 0.1 degree.
uint32_t sky_home(int32_t lat_e4, int32_t lon_e4);
// 0 new moon to 7, an eighth of a month each.
uint8_t moon_phase(int64_t epoch);
// Sun, moon, weather, scenery, seed; Weather's sky when known.
void sky_fill(AppSky &s, const char *hex, int32_t lat_e4, int32_t lon_e4, int64_t now);
// The plane's top left in sprite pixels at t.
void sky_plane_at(const AppSky &s, const flights::PlaneSprite &sp, uint32_t t, int &x, int &y);
// The scene at t ms; still holds t at 0.
void sky_paint(Px565 &p, const AppSky &s, uint32_t t);

}  // namespace espn
