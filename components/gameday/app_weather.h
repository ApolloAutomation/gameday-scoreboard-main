// The Weather display: reads, failover, pages and frames; pure.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_f1.h"
#include "apps.h"
#include "weather.h"

namespace wx {

// Its set keys, nullptr-ended; tests/web/mock_device.py reads this list.
constexpr const char *kWeatherSetKeys[] = {"wxticker", nullptr};

// Saved as it is: a new size resets it.
struct Prefs {
  uint8_t ticker{0};  // warnings in every display's ticker
  uint8_t spare[7]{};
} __attribute__((packed));
static_assert(sizeof(Prefs) == 8, "Prefs is saved as it is: a new size resets the Weather settings");

enum class Page : uint8_t { ALERTS = 0, NOW, HOURLY, DAILY, RADAR, AIR };
static const int kPages = 6;

// Words wrapped to max glyphs a line; long words cut.
std::vector<std::string> wrap(const std::string &text, size_t max);

class WeatherApp final : public espn::App {
 public:
  enum class Read : uint8_t { FORECAST = 0, ALERTS, RADAR, AIR, NONE };
  static constexpr int kReads = (int) Read::NONE;
  enum Host : uint8_t { HOST_OPEN_METEO, HOST_MET, HOST_AIR, HOST_NWS, HOST_RAINVIEWER, kHosts };

  // Seconds between reads while on screen.
  static constexpr int64_t kForecastS = 600, kAirS = 1800, kAlertsS = 300, kRadarS = 600;
  // Data this old isn't shown.
  static constexpr int64_t kOldS = 3 * 3600, kAirOldS = 2 * 3600, kRadarOldS = 30 * 60, kAlertsOldS = 3600;
  // Failed forecast reads in a row before "Can't reach".
  static constexpr uint8_t kNoDataFails = 3;
  // One radar frame's time, then the newest held.
  static constexpr uint32_t kStepMs = 500;
  static constexpr uint32_t kHoldSteps = 4;
  // Radar images failing: the other source this long.
  static constexpr int64_t kImagesOutS = 600;

  espn::Display id() const override { return espn::Display::WEATHER; }
  const char *const *hosts() const override;
  void *prefs() override { return &this->prefs_; }
  size_t prefs_size() const override { return sizeof(Prefs); }
  const char *const *set_keys() const override { return kWeatherSetKeys; }
  espn::AppSet set(const std::string &key, const std::string &value) override;
  void state(JsonObject out, int64_t now) const override;
  void shown(bool on) override;
  // A warning or watch keeps Off or dim lit.
  bool lit(int64_t now) const override;
  uint32_t poll_ms(int64_t now) const override;
  uint32_t night_poll_ms(int64_t now) const override { return this->off_poll_ms_(now); }
  uint32_t idle_poll_ms(int64_t now) const override { return this->off_poll_ms_(now); }
  bool request(int64_t now, espn::AppRequest &out) override;
  bool filter(JsonDocument &f) override;
  bool parse_doc(JsonObjectConst root, int64_t now) override;
  void ended(const espn::AppResult &r) override { this->result_ = r; }
  bool own_backoff() const override { return true; }
  void apply(bool ok, int64_t now) override;
  void dropped() override;
  void frame(int w, int h, int64_t now, espn::AppFrame &out) const override;
  uint32_t redraw_ms() const override { return this->redraw_; }
  void photo_failed(int64_t now) override;
  std::string ticker_warning(int64_t now, uint32_t &color) const override;

  // The pages to rotate now, in order.
  size_t rotation(int64_t now, Page *out, size_t cap) const;
  // The page on screen; left: its seconds to go.
  Page page(int64_t now, uint32_t *left = nullptr) const;
  // The next read and when; on: on screen.
  Read next_read(int64_t now, bool on, int64_t &at) const;
  const Forecast &forecast() const { return this->fc_; }
  const Air &air() const { return this->air_; }
  const Alerts &alerts() const { return this->al_; }
  const Radar &radar() const { return this->rad_; }
  const espn::F1Host &host(Host h) const { return this->host_[h]; }
  bool ticker_on() const { return this->prefs_.ticker != 0; }
  // Radar frames to animate, oldest first; "" none.
  size_t radar_urls(int64_t now, int w, int h, std::string *out, size_t cap) const;

 private:
  bool home_(int32_t &lat, int32_t &lon) const;
  bool for_home_(Read r, int32_t lat, int32_t lon) const;
  bool fresh_(Read r, int64_t now) const;
  bool nws_(int32_t lat, int32_t lon) const;
  bool rv_(int64_t now) const;
  bool iem_(int64_t now) const;
  int64_t due_(Read r, int64_t now, bool on) const;
  uint32_t off_poll_ms_(int64_t now) const;
  void fail_(Host h, int64_t now);
  // Alerts not yet over, for this home.
  size_t live_alerts_(int64_t now, const Alert **out, size_t cap) const;
  std::string ticker_(int64_t now, std::vector<esphome::gameday::TickerSpan> &spans) const;
  void note_(int w, int h, const char *text, espn::AppFrame &out) const;
  void now_(int w, int h, int64_t now, espn::AppFrame &out) const;
  void hourly_(int w, int h, int64_t now, espn::AppFrame &out) const;
  void daily_(int w, int h, int64_t now, espn::AppFrame &out) const;
  void radar_(int w, int h, int64_t now, espn::AppFrame &out) const;
  void alerts_(int w, int h, int64_t now, espn::AppFrame &out) const;
  void aqi_(int w, int h, espn::AppFrame &out) const;

  Prefs prefs_;
  Forecast fc_{};
  Air air_{};
  Alerts al_;
  Radar rad_;
  // Written by parse on the fetch task, taken by apply.
  Forecast p_fc_{};
  Air p_air_{};
  Alerts p_al_;
  Radar p_rad_;
  // The home each kind was read for
  int32_t lat_[kReads]{}, lon_[kReads]{};
  int64_t ok_[kReads]{};
  int64_t last_[kReads]{};
  int64_t last_any_{0};
  espn::F1Host host_[kHosts];
  // The home the NWS called outside its area
  int32_t no_nws_lat_{0}, no_nws_lon_{0};
  // Until when RainViewer's, then Iowa State's, images failed
  int64_t img_out_[2]{};
  uint8_t fails_{0};
  Read inflight_{Read::NONE};
  Host inflight_host_{HOST_OPEN_METEO};
  int32_t req_lat_{0}, req_lon_{0};
  espn::AppResult result_;
  uint32_t rnd_{0x6D2B79F5};
  bool on_{false};
  // The rotation's start and the radar's
  mutable int64_t shown_s_{0};
  mutable uint32_t radar_ms_{0};
  mutable Page last_page_{Page::NOW};
  mutable uint32_t redraw_{1000};
};

}  // namespace wx
