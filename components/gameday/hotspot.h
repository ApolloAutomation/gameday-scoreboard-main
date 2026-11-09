// Who answers a phone on the panel's own setup hotspot ("Game Day 2f6a70").
// Pure, with no ESPHome includes, so the host tests compile it directly.
//
// ESPHome's captive portal (2026.10) answers every GET with its Wi-Fi setup
// page, apart from its own /config.json and /wifisave, and its DNS server
// answers every name with the panel's address, 192.168.4.1. A phone that
// joins checks for internet by reading a known address: Android
// /generate_204 at connectivitycheck.gstatic.com, the iPhone
// /hotspot-detect.html at captive.apple.com, Windows /connecttest.txt and
// /ncsi.txt, Firefox /canonical.html, some Linux phones and laptops just /.
// The setup page in place of the answer it expects is what makes the phone
// open it, or post a "sign in to network" notification.
//
// The web server hands a request to the first handler that takes it, in the
// order they were added, and the portal adds itself when the hotspot first
// starts. On a panel that has never had a network that is during Wi-Fi's
// own setup, ahead of everything, and the portal answers every page. On a
// panel that has one, the fallback hotspot of a lost network or the setup
// after Reset Wi-Fi, the hotspot starts later and the portal is added last:
// then the device page's handler took /, which is 192.168.4.1 typed by hand,
// a probe made at / and the portal address the hotspot's DHCP hands out, and
// the phone got the scoreboard's page instead of the Wi-Fi setup.
//
// So the gameday component adds a handler ahead of all of them that decides
// here, the same way in both cases: while the portal is up and the panel is
// off Wi-Fi, every page is the setup page, except the panel's own /gameday/
// routes (its state and crash report), which a bench script can still read
// over the hotspot. On Wi-Fi, or with no hotspot, nothing changes.
#pragma once

#include <cstdint>
#include <string>

namespace esphome::gameday {

enum class HotspotRoute : uint8_t {
  USUAL,       // whoever would take it anyway: the device page, the API, the portal
  SETUP_PAGE,  // the captive portal's Wi-Fi setup page (or its /config.json, /wifisave)
  PANEL,       // the gameday component's own /gameday/ routes
};

// One of the gameday component's own routes. path: without the query.
inline bool panel_route(const std::string &path) { return path.compare(0, 9, "/gameday/") == 0; }

// portal_up: the captive portal is serving (the hotspot is up). on_wifi: the
// panel is on a network. get: the request is a GET. path: without the query.
// Only GETs: the portal takes nothing else, so a POST (a setting, an update
// upload) already goes where it always did.
inline HotspotRoute hotspot_route(bool portal_up, bool on_wifi, bool get, const std::string &path) {
  if (!portal_up || on_wifi || !get)
    return HotspotRoute::USUAL;
  return panel_route(path) ? HotspotRoute::PANEL : HotspotRoute::SETUP_PAGE;
}

}  // namespace esphome::gameday
