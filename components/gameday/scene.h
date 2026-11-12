// Pure pixel-art plays before a big play's splash
// Every frame is a function of time alone
#pragma once

#include <cstdint>

#include "play_anim.h"
#include "px565.h"

namespace espn {

constexpr uint32_t kAnimMaxMs = 3600;
constexpr uint32_t kSceneFrameMs = 33;
constexpr int kSceneBoxes = 10;

// Logical coordinates: the scoring team attacks right
struct StageGeom {
  int16_t w{0}, h{0}, s{1};
  int16_t crowd_y{0}, field_y{0}, feet_y{0};
  int16_t ez{0}, goal_x{0};
  int16_t stripe{4};
};
StageGeom stage_geom(int w, int h);

struct Kit {
  uint32_t jersey, trim, helmet;
};
// Jerseys clear of the black sky and turf
Kit kit_colors(uint32_t primary, uint32_t alt);
// The other side's kit, kept apart from mine
Kit kit_against(const Kit &mine, uint32_t primary, uint32_t alt);

struct Scene {
  AnimArgs args{};
  StageGeom g{};
  uint16_t off[16]{}, def[16]{};
  uint32_t ez_rgb{0}, crowd_a{0}, crowd_b{0};
  uint32_t seed{0};
};

// The scorer's colors, then the defense's; alt 0 none
Scene scene_make(const AnimArgs &a, int w, int h, uint32_t color, uint32_t alt, uint32_t other, uint32_t other_alt,
                 uint32_t seed);
uint32_t scene_ms(AnimKind k);
// What moves at t, on the picture; returns the count
int scene_boxes(const Scene &sc, uint32_t t, PxBox *out);
// Background then players, inside p.clip only
void scene_draw(Px565 &p, const Scene &sc, uint32_t t);

// For tests: the ball and its carrier at t, logical
struct ScenePose {
  int16_t ball_x{-1}, ball_y{-1};
  int16_t hero_x{0};
  bool ball_free{false};
};
ScenePose scene_pose(const Scene &sc, uint32_t t);

}  // namespace espn
