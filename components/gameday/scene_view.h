// The play animation's screen-sized canvas, drawn by dirty boxes
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL

#include "esphome/components/lvgl/lvgl_esphome.h"

#include "scene.h"

namespace esphome::gameday {

// done runs at its end; say holds '|' choices
bool scene_play(lv_obj_t *host, const ::espn::AnimArgs &a, uint32_t color, uint32_t other, uint32_t seed,
                void (*done)(), const char *say = nullptr, const char *score = nullptr);
void scene_stop();
bool scene_playing();

}  // namespace esphome::gameday

#endif  // USE_LVGL
