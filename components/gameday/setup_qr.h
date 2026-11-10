// Boot QR and join ticker; no ESPHome includes.
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

// Only after a join with "Show how to connect" on.
static const uint32_t kJoinTickerMs = 30 * 1000;

// Unsigned subtraction survives the millis() wrap; 0 is none.
inline bool join_ticker_on(uint32_t since_ms, uint32_t now_ms) {
  return since_ms != 0 && (uint32_t) (now_ms - since_ms) < kJoinTickerMs;
}

// IP first: some phones can't open .local names.
inline std::string join_ticker_text(const std::string &ip, const std::string &hostname) {
  if (ip.empty())
    return std::string();
  std::string out = "IP " + ip + " | ";
  if (!hostname.empty())
    out += hostname + ".local | ";
  return out;
}

// Which board layout carries the ticker (board_view.cpp).
enum class TickerHost : uint8_t { GAME, NO_GAME, BOARD };

// card: clock or leaderboard card; board: a leaderboard.
inline TickerHost ticker_host(bool no_game_screen, bool card, bool board, bool join_line) {
  if (card || (no_game_screen && join_line))
    return TickerHost::NO_GAME;
  return board ? TickerHost::BOARD : TickerHost::GAME;
}

}  // namespace esphome::gameday
