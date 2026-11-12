// Play animation GIFs for review: make -C tests review
#include <cstdio>
#include <string>
#include <vector>

#include "gif_write.h"
#include "scene_render.h"

using namespace espn;

namespace {

void render(const std::string &dir, AnimKind kind, int w, int h, const Kit2 &kit, bool mirror, int feet = 401) {
  AnimArgs a;
  a.kind = kind;
  a.flags = mirror ? kAnimMirror : 0;
  if (kind == AnimKind::HOME_RUN) {
    a.dist = (int16_t) feet;
    a.flags = 4 << kAnimFieldShift;
  }
  // The panel has no alternate colors
  const Scene sc = scene_make(a, w, h, kit.color, 0, kit.other, 0, 7);
  std::vector<uint16_t> buf((size_t) w * h);
  GifWriter g;
  g.w = w;
  g.h = h;
  g.scale = 4;
  const std::vector<uint32_t> ts = scene_times(kind);
  for (size_t i = 0; i < ts.size(); i++) {
    if (i == 0)
      scene_full(buf, sc, ts[0]);
    else
      scene_step(buf, sc, ts[i - 1], ts[i]);
    // Round each frame's end to centiseconds
    const int cs = i + 1 == ts.size() ? 150 : (int) ((ts[i + 1] + 5) / 10 - (ts[i] + 5) / 10);
    g.add(buf.data(), w, cs);
  }
  char name[128];
  snprintf(name, sizeof name, "/anim_%s_%dx%d_%s%s%s.gif", anim_name(kind), w, h, kit.name, mirror ? "_mirror" : "",
           kind == AnimKind::HOME_RUN && feet != 401 ? (feet > 401 ? "_long" : "_short") : "");
  const std::string path = dir + name;
  if (g.save(path))
    printf("%s\n", path.c_str());
  else
    fprintf(stderr, "can't write %s\n", path.c_str());
}

}  // namespace

// Optional second argument: one scene, by anim_name
int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "build/review";
  const std::string only = argc > 2 ? argv[2] : "";
  for (AnimKind k : {AnimKind::TD_PASS, AnimKind::TD_RUN, AnimKind::TD_RETURN, AnimKind::HOME_RUN,
                     AnimKind::SOCCER_GOAL, AnimKind::HOCKEY_GOAL}) {
    if (!only.empty() && only != anim_name(k))
      continue;
    for (const auto &s : kSceneScreens) {
      for (const Kit2 &kit : kReviewKits)
        render(dir, k, s[0], s[1], kit, false);
      if (k != AnimKind::HOME_RUN)
        render(dir, k, s[0], s[1], kReviewKits[0], true);
    }
  }
  if (!only.empty())
    return 0;
  for (const auto &s : kSceneScreens) {
    render(dir, AnimKind::HOME_RUN, s[0], s[1], kReviewKits[1], false, 440);
    render(dir, AnimKind::HOME_RUN, s[0], s[1], kReviewKits[1], false, 360);
  }
  return 0;
}
