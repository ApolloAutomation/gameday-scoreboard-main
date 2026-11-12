// Where each display joins the firmware; pure, as apps.h is.
#include "apps.h"
#include "app_f1.h"

namespace espn {

// One app_register(slots, app_new<...>()) per display.
void register_apps(AppSlots &slots) { app_register(slots, app_new<F1App>()); }

}  // namespace espn
