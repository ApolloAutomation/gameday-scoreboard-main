// ESPHome's portal loses / when added after the device page.
#pragma once

#include <cstdint>
#include <string>

namespace esphome::gameday {

enum class HotspotRoute : uint8_t {
  USUAL,       // whoever would take it anyway
  SETUP_PAGE,  // the captive portal's Wi-Fi setup page
  PANEL,       // the gameday component's own /gameday/ routes
};

// path: without the query.
inline bool panel_route(const std::string &path) { return path.compare(0, 9, "/gameday/") == 0; }

// Only GETs: the portal takes nothing else.
inline HotspotRoute hotspot_route(bool portal_up, bool on_wifi, bool get, const std::string &path) {
  if (!portal_up || on_wifi || !get)
    return HotspotRoute::USUAL;
  return panel_route(path) ? HotspotRoute::PANEL : HotspotRoute::SETUP_PAGE;
}

}  // namespace esphome::gameday
