#include "scene_view.h"

#ifdef USE_LVGL

#include <esp_heap_caps.h>

#include <new>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::gameday {

namespace {

const char *const TAG = "gameday.scene";
// After a stall this long, the whole frame redraws
constexpr uint32_t kResyncMs = 180;

struct SceneState {
  lv_obj_t *canvas{nullptr};
  lv_timer_t *timer{nullptr};
  uint16_t *buf{nullptr};
  int16_t w{0}, h{0};
  ::espn::Scene sc{};
  uint32_t t0{0}, t_prev{0}, ms{0};
  void (*done)(){nullptr};
  bool on{false};
  uint32_t frames{0}, worst_ms{0}, max_px{0};
};

SceneState *state = nullptr;

void end_scene(bool ran) {
  SceneState &s = *state;
  if (!s.on)
    return;
  s.on = false;
  lv_timer_pause(s.timer);
  lv_obj_add_flag(s.canvas, LV_OBJ_FLAG_HIDDEN);
  ESP_LOGD(TAG, "Scene: %s %dx%d %u frames, worst tick %u ms, max dirty %u px%s",
           ::espn::anim_name(s.sc.args.kind), s.w, s.h, (unsigned) s.frames, (unsigned) s.worst_ms,
           (unsigned) s.max_px, ran ? "" : ", stopped");
  if (ran && s.done != nullptr)
    s.done();
}

void draw_box(const ::espn::PxBox &b, uint32_t t) {
  SceneState &s = *state;
  ::espn::Px565 p;
  p.px = s.buf;
  p.w = p.stride = s.w;
  p.h = s.h;
  p.clip = ::espn::box_clip(b, ::espn::PxBox{0, 0, s.w, s.h});
  if (p.clip.empty())
    return;
  ::espn::scene_draw(p, s.sc, t);
  lv_area_t a;
  lv_area_set(&a, p.clip.x, p.clip.y, p.clip.x + p.clip.w - 1, p.clip.y + p.clip.h - 1);
  lv_obj_invalidate_area(s.canvas, &a);
}

void tick(lv_timer_t *) {
  SceneState &s = *state;
  if (!s.on)
    return;
  const uint32_t start = millis();
  const uint32_t t = start - s.t0;
  if (t >= s.ms) {
    end_scene(true);
    return;
  }
  ::espn::PxBox boxes[2 * ::espn::kSceneBoxes];
  int n = 0;
  if (t - s.t_prev > kResyncMs) {
    boxes[n++] = ::espn::PxBox{0, 0, s.w, s.h};
  } else {
    n = ::espn::scene_boxes(s.sc, s.t_prev, boxes);
    n += ::espn::scene_boxes(s.sc, t, boxes + n);
  }
  // Overlapping boxes merge, so no pixel draws twice
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (::espn::box_overlap(boxes[i], boxes[j])) {
        boxes[i] = ::espn::box_join(boxes[i], boxes[j]);
        boxes[j] = boxes[--n];
        j = i;
      }
  uint32_t px = 0;
  for (int i = 0; i < n; i++) {
    draw_box(boxes[i], t);
    px += (uint32_t) boxes[i].w * boxes[i].h;
  }
  s.t_prev = t;
  s.frames++;
  const uint32_t took = millis() - start;
  if (took > s.worst_ms)
    s.worst_ms = took;
  if (px > s.max_px)
    s.max_px = px;
}

bool ensure(lv_obj_t *screen, int w, int h) {
  if (state == nullptr) {
    void *mem = heap_caps_calloc(1, sizeof(SceneState), MALLOC_CAP_SPIRAM);
    if (mem == nullptr)
      return false;
    state = new (mem) SceneState();
  }
  SceneState &s = *state;
  if (s.buf != nullptr && (s.w != w || s.h != h)) {
    heap_caps_free(s.buf);
    s.buf = nullptr;
  }
  if (s.buf == nullptr) {
    s.buf = (uint16_t *) heap_caps_malloc((size_t) w * h * 2, MALLOC_CAP_SPIRAM);
    if (s.buf == nullptr)
      return false;
    s.w = (int16_t) w;
    s.h = (int16_t) h;
    if (s.canvas != nullptr)
      lv_canvas_set_buffer(s.canvas, s.buf, w, h, LV_COLOR_FORMAT_RGB565);
  }
  if (s.canvas == nullptr || lv_obj_get_screen(s.canvas) != screen) {
    if (s.canvas != nullptr)
      lv_obj_delete(s.canvas);
    s.canvas = lv_canvas_create(screen);
    lv_obj_remove_flag(s.canvas, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_canvas_set_buffer(s.canvas, s.buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_flag(s.canvas, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_pos(s.canvas, 0, 0);
  lv_obj_set_size(s.canvas, w, h);
  if (s.timer == nullptr) {
    s.timer = lv_timer_create(tick, ::espn::kSceneFrameMs, nullptr);
    lv_timer_pause(s.timer);
  }
  return true;
}

}  // namespace

bool scene_play(lv_obj_t *host, const ::espn::AnimArgs &a, uint32_t color, uint32_t other, uint32_t seed,
                void (*done)(), const char *say, const char *score) {
  if (host == nullptr || ::espn::scene_ms(a.kind) == 0)
    return false;
  lv_obj_t *screen = lv_obj_get_screen(host);
  // Another page, or the guard's: no scene
  if (screen != lv_screen_active())
    return false;
  lv_display_t *d = lv_obj_get_display(host);
  const int w = lv_display_get_horizontal_resolution(d), h = lv_display_get_vertical_resolution(d);
  if (w < 64 || h < 32 || w > 256 || h > 128)
    return false;
  if (state != nullptr && state->on)
    end_scene(false);
  if (!ensure(screen, w, h))
    return false;
  SceneState &s = *state;
  s.sc = ::espn::scene_make(a, w, h, color, 0, other, 0, seed);
  ::espn::scene_say(s.sc, say, score);
  s.ms = ::espn::scene_ms(a.kind);
  s.done = done;
  s.t0 = millis();
  s.t_prev = 0;
  s.frames = s.worst_ms = s.max_px = 0;
  // The first frame whole, as a splash draws
  draw_box(::espn::PxBox{0, 0, s.w, s.h}, 0);
  lv_obj_move_foreground(s.canvas);
  lv_obj_remove_flag(s.canvas, LV_OBJ_FLAG_HIDDEN);
  s.on = true;
  lv_timer_resume(s.timer);
  lv_timer_reset(s.timer);
  return true;
}

void scene_stop() {
  if (state != nullptr)
    end_scene(false);
}

bool scene_playing() { return state != nullptr && state->on; }

}  // namespace esphome::gameday

#endif  // USE_LVGL
