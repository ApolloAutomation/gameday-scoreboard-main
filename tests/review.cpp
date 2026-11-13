// Play animation GIFs for review: make -C tests review
#include <cstdio>
#include <string>
#include <vector>

#include "gif_write.h"
#include "scene_render.h"

using namespace espn;

namespace {

void render(const std::string &dir, const ScenePlay &play, int w, int h, const Kit2 &kit, bool named, bool mirror,
            int feet = 0) {
  // The panel has no alternate colors
  const Kit2 bare{kit.name, kit.color, 0, kit.other, 0};
  const Scene sc = scene_of(play, w, h, bare, named, mirror, feet);
  std::vector<uint16_t> buf((size_t) w * h);
  GifWriter g;
  g.w = w;
  g.h = h;
  g.scale = 4;
  const std::vector<uint32_t> ts = scene_times(sc.args.kind);
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
  snprintf(name, sizeof name, "/anim_%s_%dx%d_%s%s%s.gif", anim_name(sc.args.kind), w, h, kit.name,
           mirror ? "_mirror" : "", feet == 0 ? "" : feet > 401 ? "_long" : "_short");
  const std::string path = dir + name;
  if (g.save(path))
    printf("%s  \"%s\" \"%s\"\n", path.c_str(), sc.say, sc.score);
  else
    fprintf(stderr, "can't write %s\n", path.c_str());
}

}  // namespace

// Optional second argument: one scene, by anim_name
int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "build/review";
  const std::string only = argc > 2 ? argv[2] : "";
  for (const ScenePlay &p : kScenePlays) {
    const AnimKind k = anim_sample(p.play, "", 0).args.kind;
    if (!only.empty() && only != anim_name(k))
      continue;
    for (const auto &s : kSceneScreens) {
      // Navy: the Try it caption; the rest name a scorer
      for (int i = 0; i < 4; i++)
        render(dir, p, s[0], s[1], kReviewKits[i], i != 0, false);
      if (k != AnimKind::HOME_RUN)
        render(dir, p, s[0], s[1], kReviewKits[1], true, true);
      else
        for (int feet : {440, 360})
          render(dir, p, s[0], s[1], kReviewKits[1], true, false, feet);
    }
  }
  return 0;
}
