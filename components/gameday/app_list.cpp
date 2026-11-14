// Where each display joins the firmware; pure, as apps.h is.
#include "apps.h"

#include "app_f1.h"
#include "app_flights.h"
#include "app_markets.h"
#include "app_weather.h"

namespace espn {

// One app_register(slots, app_new<...>()) per display.
void register_apps(AppSlots &slots) {
  app_register(slots, app_new<F1App>());
  app_register(slots, app_new<MarketsApp>());
  app_register(slots, app_new<::flights::FlightsApp>());
  app_register(slots, app_new<::wx::WeatherApp>());
}

}  // namespace espn
