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

// What the boot screen says while off Wi-Fi.
enum class OffWifiView : uint8_t { CONNECTING, JOINING, CANT_JOIN, LOST, SETUP };

static constexpr uint32_t FIRST_JOIN_WAIT_MS = 30000;

// A network given since a start that had none, never joined.
inline bool first_join(bool named, bool net_at_start, bool was_on) { return named && !net_at_start && !was_on; }

// had_creds: latched by the caller, never on a first join.
inline OffWifiView off_wifi_view(bool named, bool net_at_start, bool was_on, bool had_creds, uint32_t up_ms,
                                 uint32_t join_ms, bool setup) {
  if (first_join(named, net_at_start, was_on))
    return join_ms < FIRST_JOIN_WAIT_MS ? OffWifiView::JOINING : OffWifiView::CANT_JOIN;
  if (had_creds && (was_on || up_ms > 30000))
    return OffWifiView::LOST;
  return setup ? OffWifiView::SETUP : OffWifiView::CONNECTING;
}

}  // namespace esphome::gameday
