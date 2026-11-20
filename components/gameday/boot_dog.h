// Apollo on the start-up screen: his canvas
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL

#include "esphome/components/lvgl/lvgl_esphome.h"

#include "boot_brand.h"

namespace esphome::gameday {

// Shows him at (x, y) under parent's other widgets
bool boot_dog_show(lv_obj_t *parent, int x, int y);
void boot_dog_hide();

}  // namespace esphome::gameday

#endif  // USE_LVGL
