// The F1 display: its reads, failover and screens; pure.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "apps.h"
#include "f1.h"

namespace espn {

// MET Norway's compact forecast, the weather backup.
std::string f1_met_url(int32_t lat_e4, int32_t lon_e4);
// MET Norway asks for the product and a contact.
std::string f1_met_agent(const std::string &version);
// "lightrainshowers_day" as a WMO weather code.
uint8_t met_weather_code(const std::string &symbol);

namespace detail {
void fill_met_filter(JsonDocument &f);
int met_weather_from_doc(JsonObjectConst root, int64_t now, F1Weather &out);
}

// The forecast hour nearest now: 1 read, -1 not one.
template<typename TInput> int parse_met_weather(TInput &input, int64_t now, F1Weather &out) {
  return detail::f1_parse(input, detail::fill_met_filter,
                          [&out, now](JsonObjectConst r) { return detail::met_weather_from_doc(r, now, out); });
}

// The header's weekend as a schedule: 1 read, 0 none.
int f1_header_schedule(JsonObjectConst root, const F1Live &live, F1Schedule &out);
// ESPN's calendar's next weekend: first practice and race.
F1Schedule f1_calendar_schedule(const GolfNext &next);
// US and UK: mph and miles; US alone: Fahrenheit.
F1Units f1_units_for_tz(uint8_t tz);
// left, then right flush to chars.
std::string f1_cell(const std::string &left, const std::string &right, size_t chars);
// "45m", "5h", "3d".
std::string f1_age_text(int64_t seconds);

// Failures in a row before a host's backup takes over.
static const uint8_t kF1FailOver = 3;

// One host's waits; its backup after failures or a refusal.
struct F1Host {
  uint8_t fails{0};
  uint8_t outs{0};  // failovers since it last answered
  bool refused{false};
  int64_t wait_until{0};
  int64_t out_until{0};  // the next try of a host failed over

  void ok() { *this = F1Host{}; }
  // True when this failure moved it to its backup.
  bool fail(int64_t now, bool refused, uint32_t retry_after_ms, uint32_t rnd, bool failover);
  bool backup() const { return this->outs > 0; }
  bool may_read(int64_t now) const {
    return now >= this->wait_until && (this->outs == 0 || now >= this->out_until);
  }
};

class F1App final : public App {
 public:
  enum class Read : uint8_t { HEADER, NEXT, DRIVERS, CONSTRUCTORS, RESULTS, CIRCUIT, ATHLETE, WEATHER, CALENDAR, NONE };
  static constexpr int kReads = (int) Read::NONE;
  enum Host : uint8_t { HOST_ESPN, HOST_JOLPICA, HOST_OPEN_METEO, HOST_MET, kHosts };

  // Out of line, built for size: its members are large.
  F1App();
  Display id() const override { return Display::F1; }
  const char *const *hosts() const override;
  void state(JsonObject out) const override;
  bool busy(int64_t now) const override;
  uint32_t poll_ms(int64_t now) const override;
  bool request(int64_t now, AppRequest &out) override;
  bool filter(JsonDocument &f) override;
  bool parse_doc(JsonObjectConst root, int64_t now) override;
  void ended(const AppResult &r) override { this->result_ = r; }
  bool own_backoff() const override { return true; }
  void apply(bool ok, int64_t now) override;
  void frame(int w, int h, int64_t now, AppFrame &out) const override;
  uint32_t redraw_ms() const override { return this->redraw_; }

  // The screens to rotate now, in order.
  size_t rotation(int64_t now, F1Screen *out, size_t cap) const;
  // The card on screen; left: its seconds to go.
  F1Screen screen(int64_t now, uint32_t *left = nullptr) const;
  // The next read and when it is due.
  Read next_read(int64_t now, int64_t &at) const;
  const F1Schedule &schedule(int64_t now) const;
  const CircuitInfo *circuit(int64_t now) const;
  const F1Host &host(Host h) const { return this->host_[h]; }
  const F1Live &live() const { return this->live_; }
  bool weather_backup() const { return this->weather_backup_; }

 private:
  bool weather_place_(int64_t now, int32_t &lat, int32_t &lon) const;
  bool jolpica_out_() const;
  bool fresh_weather_(int64_t now) const;
  bool stale_standings_(int64_t now) const;
  bool live_card_(int64_t now) const;
  int64_t due_(Read r, int64_t now) const;
  void fail_(Host h, int64_t now);
  void live_frame_(int w, int64_t now, AppFrame &out) const;
  void next_frame_(int w, int64_t now, AppFrame &out) const;
  void circuit_frame_(int w, int h, int64_t now, AppFrame &out) const;
  void weather_frame_(int w, int64_t now, AppFrame &out) const;
  void standings_frame_(const F1Standings &s, int w, int64_t now, AppFrame &out) const;
  void results_frame_(int w, AppFrame &out) const;
  std::string ticker_(int64_t now, std::vector<esphome::gameday::TickerSpan> &spans) const;
  std::string place_(int64_t now) const;

  F1Live live_;
  F1Schedule sched_, head_sched_, cal_sched_;
  F1Standings drivers_, teams_;
  F1Results results_;
  F1CircuitFacts facts_;
  F1Weather weather_;
  int32_t weather_lat_{0}, weather_lon_{0};
  bool weather_backup_{false};

  // Written by parse on the fetch task, taken by apply.
  F1Live p_live_;
  F1Schedule p_sched_;
  F1Standings p_stand_;
  F1Results p_results_;
  F1CircuitFacts p_facts_;
  F1Athlete p_athlete_;
  F1Weather p_weather_;
  GolfNext p_next_;

  Read inflight_{Read::NONE};
  Host inflight_host_{HOST_ESPN};
  int32_t req_lat_{0}, req_lon_{0};
  uint32_t facts_try_{0}, athlete_try_{0};  // the ids last asked for
  std::string cal_after_;
  int64_t cal_after_start_{0};
  int64_t last_[kReads]{};
  int64_t ok_[kReads]{};
  int64_t last_any_{0};
  F1Host host_[kHosts];
  AppResult result_;
  uint32_t rnd_{0x2545F491};
  // Until the drawn frame changes: its card or countdown.
  mutable uint32_t redraw_{1000};
};

}
