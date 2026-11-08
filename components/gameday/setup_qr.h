// What the boot screen's QR code holds (firmware/pages/boot.yaml), and the
// ticker's address line after the panel joins Wi-Fi. A phone's own camera
// reads both kinds of code, so setting a panel up needs no app: the camera
// and a browser are enough. Pure, with no ESPHome includes, so the host
// tests compile it directly.
#pragma once

#include <cstdint>
#include <string>

namespace esphome::gameday {

// A Wi-Fi join code for an open network, the kind a phone shows to share its
// Wi-Fi: WIFI:T:nopass;S:<name>;; with \ ; , : and " in the name escaped by
// a backslash. The iPhone and Android cameras both offer to join it. The
// phone then offers ESPHome's captive portal to pick the home network: an
// iPhone opens it, and many Android phones post a sign-in notification
// instead. It is at http://192.168.4.1/ either way.
// The hotspot's name always starts "Game Day " (gameday-common.yaml,
// on_boot), so it can never be read as a hex name and needs no quotes.
inline std::string wifi_join_qr(const std::string &ssid) {
  std::string out = "WIFI:T:nopass;S:";
  for (char c : ssid) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
      out += '\\';
    out += c;
  }
  out += ";;";
  return out;
}

// The panel's own device page, by IP address: http://192.168.1.50. Not the
// .local name, which many Android phones can't open. No trailing slash: a
// browser adds it, and at 20 bytes or less (a typical home address) LVGL
// can encode the code at error correction Q instead of M (boot.yaml). Empty
// without an address, and then the boot screen shows no code.
inline std::string device_page_qr(const std::string &ip) {
  if (ip.empty())
    return std::string();
  return "http://" + ip;
}

// After a join, once the connect screen has gone (or straight away over a
// live game, which the connect screen leaves alone), the ticker starts with
// the panel's address for this long. Only where the connect screen would
// show the address: a new join with "Show how to connect" on (boot.yaml).
static const uint32_t kJoinTickerMs = 30 * 1000;

// Whether that line is still due: since_ms is millis() when it started, 0
// for none. Unsigned subtraction keeps it right across the millis() wrap.
inline bool join_ticker_on(uint32_t since_ms, uint32_t now_ms) {
  return since_ms != 0 && (uint32_t) (now_ms - since_ms) < kJoinTickerMs;
}

// The line itself, ahead of the rest of the ticker: the IP first, which
// every phone can open, then the .local name, which some can't. Empty
// without an IP. "IP 192.168.1.50 | gameday-2f6a70.local | "
inline std::string join_ticker_text(const std::string &ip, const std::string &hostname) {
  if (ip.empty())
    return std::string();
  std::string out = "IP " + ip + " | ";
  if (!hostname.empty())
    out += hostname + ".local | ";
  return out;
}

}  // namespace esphome::gameday
