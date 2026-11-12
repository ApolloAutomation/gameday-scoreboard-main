// The Flight Tracker display: settings, reads and screens; pure.
#pragma once

#include <cstdint>
#include <string>

#include "apps.h"
#include "flights.h"

namespace flights {

// Its set keys, nullptr-ended; tests/web/mock_device.py reads this list.
constexpr const char *kFlightSetKeys[] = {"flyhome", "flyplace", "flyradius", "flyreceiver", nullptr};

static const int32_t kNoCoord = INT32_MIN;
static const uint16_t kRadiusDefault = 10;
static const size_t kPlaceMax = 47;
static const size_t kReceiverMax = 95;

// Saved as it is: a new size resets it.
struct Prefs {
  int32_t lat_e4{kNoCoord};  // degrees times 10,000
  int32_t lon_e4{kNoCoord};
  uint16_t radius{kRadiusDefault};
  uint8_t radius_unit{0};  // DistUnit
  uint8_t spare0{0};
  char place[kPlaceMax + 1]{};
  char receiver[kReceiverMax + 1]{};
  uint8_t spare[8]{};
} __attribute__((packed));
static_assert(sizeof(Prefs) == 164, "Prefs is saved as it is: a new size resets the Flight Tracker's settings");

// Each unit's largest radius, all within kMaxRadiusNm.
uint16_t radius_max(DistUnit u);
const char *dist_unit_key(DistUnit u);
Home prefs_home(const Prefs &p);
// Out-of-range or unterminated fields back to defaults.
void prefs_clean(Prefs &p);
// "40.6413,-73.7781"; "" when unset.
std::string coord_text(int32_t lat_e4, int32_t lon_e4);
// "lat,lon", "lat, lon" or "lat lon", read digit by digit.
bool parse_coords(const std::string &v, int32_t &lat_e4, int32_t &lon_e4);
// "10", "10mi", "25km" or "5nm"; capped at radius_max.
bool parse_radius(const std::string &v, uint16_t &n, uint8_t &unit);
// US and UK time zones: feet, mph and miles.
Units units_for_zone(int tz_index);
// Shared by the display and the settings tests.
espn::AppSet prefs_set(Prefs &p, const std::string &key, const std::string &value);
void prefs_state(const Prefs &p, const Units &u, JsonObject out);

class FlightsApp final : public espn::App {
 public:
  // Each screen's time where the card and list take turns.
  static const uint32_t kScreenMs = 8000;
  // Data this old shows a note, not planes.
  static const int64_t kOldS = 180;
  // Failed position reads in a row before "Can't reach".
  static const uint8_t kNoDataFails = 3;

  espn::Display id() const override { return espn::Display::FLIGHTS; }
  const char *const *hosts() const override;
  void *prefs() override { return &this->prefs_; }
  size_t prefs_size() const override { return sizeof(Prefs); }
  void setup() override;
  const char *const *set_keys() const override { return kFlightSetKeys; }
  espn::AppSet set(const std::string &key, const std::string &value) override;
  void state(JsonObject out) const override;
  void shown(bool on) override;
  bool own_backoff() const override { return true; }
  void ended(const espn::AppResult &r) override {
    this->status_ = r.status;
    this->retry_ms_ = r.retry_after_ms;
  }
  uint32_t poll_ms(int64_t now) const override;
  bool request(int64_t now, espn::AppRequest &out) override;
  bool parse(espn::AppStream &in, int64_t now) override;
  void apply(bool ok, int64_t now) override;
  void frame(int w, int h, int64_t now, espn::AppFrame &out) const override;
  uint32_t redraw_ms() const override { return kScreenMs; }

  const Nearby &nearby() const { return this->near_; }
  Source source() const { return this->got_src_; }
  const Feed &feed() const { return this->feed_; }

 private:
  enum class Read : uint8_t { NONE = 0, POSITIONS, ROUTESET, ADSBDB };
  struct Next {
    Read read{Read::NONE};
    Source source{Source::FR24};
    uint32_t in_ms{UINT32_MAX};
  };
  Next next_(int64_t now) const;
  // A read that never came back counts as failed.
  void lost_(uint32_t now_ms);
  uint32_t rnd_();
  Units units_() const { return units_for_zone(espn::app_env().tz); }

  Prefs prefs_;
  Feed feed_;
  Nearby near_;
  Nearby pending_;  // the fetch task's
  mutable RouteCache routes_;
  RouteCache pending_routes_;
  espn::HostWait im_wait_, db_wait_;
  uint32_t im_last_ms_{0}, db_last_ms_{0};
  // The read in flight, as request() left it
  Read read_{Read::NONE};
  Source read_src_{Source::FR24};
  Home read_home_;
  RouteAsk ask_;
  char db_callsign_[9]{};
  int status_{-1};
  uint32_t retry_ms_{0};
  int64_t last_req_s_{0};
  // The planes shown and where they came from
  int64_t got_s_{0};
  Source got_src_{Source::FR24};
  uint8_t fails_{0};
  uint8_t card_{0};
  char card_hex_[7]{};
  uint32_t seed_{0x9E3779B9u};
  mutable bool list_{false};
  mutable int64_t screen_s_{0};
};

}  // namespace flights
