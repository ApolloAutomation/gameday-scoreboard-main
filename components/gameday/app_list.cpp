// Where each display joins the firmware; pure, as apps.h is.
#include "apps.h"

namespace espn {

// One app_register(slots, app_new<...>()) per display.
void register_apps(AppSlots &slots) { (void) slots; }

}  // namespace espn
