// Pure pixel-art plays before a big play's splash
// Every frame is a function of time alone
#pragma once

#include <cstdint>

#include "play_anim.h"
#include "px565.h"

namespace espn {

constexpr uint32_t kAnimMaxMs = 4800;
constexpr uint32_t kSceneFrameMs = 33;
constexpr int kSceneBoxes = 12;

// Logical coordinates: the scoring team attacks right
struct StageGeom {
  int16_t w{0}, h{0}, s{1};
  int16_t crowd_y{0}, field_y{0}, feet_y{0};
  int16_t ez{0}, goal_x{0};
  int16_t stripe{4};
  // A goal's net, side on: goal_x is its post
  int16_t bar_y{0}, roof_x{0}, back_x{0};
};
StageGeom stage_geom(int w, int h);

struct Kit {
  uint32_t jersey, trim, helmet;
};
constexpr uint32_t kIceRgb = 0xB0BCC8;
// Jerseys clear of the black sky and turf
Kit kit_colors(uint32_t primary, uint32_t alt);
// The other side's kit, kept apart from mine
Kit kit_against(const Kit &mine, uint32_t primary, uint32_t alt);
// A kit clear of the ice, from kit_colors's
Kit ice_kit(Kit k, uint32_t primary, uint32_t alt);

struct Scene {
  AnimArgs args{};
  StageGeom g{};
  uint16_t off[16]{}, def[16]{}, keeper[16]{};
  // The GOAL word, and its white flash
  uint16_t word[16]{}, word_hi[16]{};
  uint32_t ez_rgb{0}, crowd_a{0}, crowd_b{0};
  uint32_t seed{0};
  // The caption (scene_say): the words that fit, the score
  char say[40]{}, score[20]{};
};

// The scorer's colors, then the defense's; alt 0 none
Scene scene_make(const AnimArgs &a, int w, int h, uint32_t color, uint32_t alt, uint32_t other, uint32_t other_alt,
                 uint32_t seed);
// Picks the first of words ('|' between) that fits
void scene_say(Scene &sc, const char *words, const char *score);
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
  bool flash{false};  // a whole-word flash: a big redraw
  PxBox word{};       // on the picture, never mirrored
  PxBox cap[2]{};     // the caption's lines, on the picture
};
// Word length; glyph i: 0 G to 3 L
int word_letters(const StageGeom &g);
int word_glyph(int i, int n);
ScenePose scene_pose(const Scene &sc, uint32_t t);

}  // namespace espn
