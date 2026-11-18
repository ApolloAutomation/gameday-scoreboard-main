// Apollo on the start-up screen: his canvas and his blink
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL

#include "esphome/components/lvgl/lvgl_esphome.h"

#include "boot_brand.h"

namespace esphome::gameday {

// Shows him at (x, y) under parent's other widgets
bool boot_dog_show(lv_obj_t *parent, int x, int y, bool ledge);
void boot_dog_hide();
// Repaints only his eyes when the blink changes
void boot_dog_blink(uint32_t now_ms, uint32_t seed);
// Eyes open, before the blink stops
void boot_dog_open();

}  // namespace esphome::gameday

#endif  // USE_LVGL
