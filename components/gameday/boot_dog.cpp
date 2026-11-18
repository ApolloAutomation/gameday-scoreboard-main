#include "boot_dog.h"

#ifdef USE_LVGL

#include <esp_heap_caps.h>

namespace esphome::gameday {

namespace {

lv_obj_t *canvas = nullptr;
uint16_t *buf = nullptr;
bool ledge_on = false, shut = false;

void paint(const ::espn::PxBox &clip) {
  ::espn::Px565 p;
  p.px = buf;
  p.w = p.h = p.stride = kDogSize;
  p.clip = clip;
  paint_apollo(p, shut, ledge_on);
}

}  // namespace

bool boot_dog_show(lv_obj_t *parent, int x, int y, bool ledge) {
  if (buf == nullptr) {
    buf = static_cast<uint16_t *>(
        heap_caps_malloc((size_t) kDogSize * kDogSize * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr)
      return false;
    ledge_on = ledge;
    paint(::espn::PxBox{0, 0, kDogSize, kDogSize});
  }
  if (canvas == nullptr) {
    canvas = lv_canvas_create(parent);
    lv_obj_remove_flag(canvas, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_canvas_set_buffer(canvas, buf, kDogSize, kDogSize, LV_COLOR_FORMAT_RGB565);
    // Under the code's box and the words
    lv_obj_move_to_index(canvas, 0);
  }
  if (ledge != ledge_on) {
    ledge_on = ledge;
    paint(::espn::PxBox{0, 0, kDogSize, kDogSize});
    lv_obj_invalidate(canvas);
  }
  if (lv_obj_get_x(canvas) != x || lv_obj_get_y(canvas) != y)
    lv_obj_set_pos(canvas, x, y);
  if (lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN))
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  return true;
}

void boot_dog_hide() {
  if (canvas != nullptr && !lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN))
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
}

void boot_dog_blink(uint32_t now_ms, uint32_t seed) {
  if (canvas == nullptr || lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN))
    return;
  const bool on = blink_on(now_ms, seed);
  if (on == shut)
    return;
  shut = on;
  const ::espn::PxBox b = apollo_blink_box();
  paint(b);
  lv_area_t c;
  lv_obj_get_coords(canvas, &c);
  lv_area_t a;
  lv_area_set(&a, c.x1 + b.x, c.y1 + b.y, c.x1 + b.x + b.w - 1, c.y1 + b.y + b.h - 1);
  lv_obj_invalidate_area(canvas, &a);
}

}  // namespace esphome::gameday

#endif  // USE_LVGL
