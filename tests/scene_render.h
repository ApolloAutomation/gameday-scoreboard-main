// Renders scenes as the panel does, box by box
#pragma once

#include <cstdint>
#include <vector>

#include "scene.h"

namespace espn {

struct Kit2 {
  const char *name;
  uint32_t color, alt, other, other_alt;
};

// Navy/white, green/gold, black/orange, white/red
constexpr Kit2 kReviewKits[] = {{"navy", 0x14284B, 0xFFFFFF, 0x9E1B32, 0xF2B705},
                                {"green", 0x1F5A30, 0xF2B705, 0x14284B, 0xC0C0C0},
                                {"black", 0x000000, 0xF26522, 0x1E6FC8, 0xB0B0B0},
                                {"white", 0xFFFFFF, 0xC8102E, 0x101828, 0x5FB030}};

constexpr int kSceneScreens[][2] = {{64, 64}, {128, 64}, {192, 64}, {256, 64}, {128, 128}};

// Each Try it play, its team, score and scorer
struct ScenePlay {
  const char *play, *abbr;
  int score;
  const char *name;
};
constexpr ScenePlay kScenePlays[] = {{"pass", "BUF", 21, "Chase"},  {"run", "BUF", 21, "Pollard"},
                                     {"ret", "BUF", 21, "Laube"},   {"hr", "NYY", 3, "Stanton"},
                                     {"soccer", "ARS", 1, "Saka"},  {"hockey", "COL", 2, "MacKinnon"},
                                     {"fg", "BUF", 21, "Prater"},   {"pat", "BUF", 27, "Prater"}};

// The kit's index picks the scorer's name or none
inline Scene scene_of(const ScenePlay &p, int w, int h, const Kit2 &kit, bool named, bool mirror, int feet = 0) {
  AnimSample a = anim_sample(p.play, p.abbr, p.score, named ? p.name : "");
  if (mirror)
    a.args.flags |= kAnimMirror;
  if (feet > 0) {
    a.args.dist = (int16_t) feet;
    a = AnimSample{a.args, say_words(a.args, Say::PLAIN, named ? p.name : "", 2), a.score};
  }
  Scene sc = scene_make(a.args, w, h, kit.color, kit.alt, kit.other, kit.other_alt, 7);
  scene_say(sc, a.say.c_str(), a.score.c_str());
  return sc;
}

inline std::vector<uint32_t> scene_times(AnimKind k) {
  std::vector<uint32_t> ts;
  const uint32_t ms = scene_ms(k);
  for (uint32_t n = 0;; n++) {
    const uint32_t t = n * 1000 / 30;
    ts.push_back(t < ms ? t : ms);
    if (t >= ms)
      break;
  }
  return ts;
}

inline void scene_full(std::vector<uint16_t> &buf, const Scene &sc, uint32_t t) {
  Px565 p;
  p.px = buf.data();
  p.w = p.stride = sc.g.w;
  p.h = sc.g.h;
  p.clip = PxBox{0, 0, p.w, p.h};
  scene_draw(p, sc, t);
}

// Redraws both frames' boxes; returns pixels touched
inline int scene_step(std::vector<uint16_t> &buf, const Scene &sc, uint32_t t_prev, uint32_t t) {
  PxBox boxes[2 * kSceneBoxes];
  int n = scene_boxes(sc, t_prev, boxes);
  n += scene_boxes(sc, t, boxes + n);
  std::vector<uint8_t> mark((size_t) sc.g.w * sc.g.h);
  int touched = 0;
  for (int i = 0; i < n; i++) {
    Px565 p;
    p.px = buf.data();
    p.w = p.stride = sc.g.w;
    p.h = sc.g.h;
    p.clip = boxes[i];
    scene_draw(p, sc, t);
    const PxBox b = box_clip(boxes[i], PxBox{0, 0, sc.g.w, sc.g.h});
    for (int y = b.y; y < b.y + b.h; y++)
      for (int x = b.x; x < b.x + b.w; x++)
        if (!mark[(size_t) y * sc.g.w + x]) {
          mark[(size_t) y * sc.g.w + x] = 1;
          touched++;
        }
  }
  return touched;
}

inline uint32_t fnv1a(uint32_t h, const std::vector<uint16_t> &buf) {
  for (uint16_t v : buf) {
    h = (h ^ (v & 255)) * 16777619u;
    h = (h ^ (v >> 8)) * 16777619u;
  }
  return h;
}

}  // namespace espn
