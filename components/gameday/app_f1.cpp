// The F1 display: reads, failover, rotation and screens.
#include "app_f1.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board_layout.h"
#include "poll_timing.h"
#include "timezones.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace espn {

using detail::add_part;
using esphome::gameday::TickerSpan;

static const uint32_t kWhite = 0xFFFFFF;
static const uint32_t kGray = 0xA0A0A0;
static const uint32_t kDim = 0x606060;
static const uint32_t kGold = 0xFFD000;
static const uint32_t kAmber = 0xFFA000;
static const uint32_t kRed = 0xFF3030;
static const int64_t kHour = 3600;
static const int64_t kNever = INT64_MAX;
// Leaderboard cells in characters (rows_geom).
static const size_t kNameWide = 11, kValue = 3, kDetail = 6, kCellNarrow = 10;
static const char *const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void put(char *dst, size_t cap, const std::string &s) { GolfRow::put(dst, cap, s); }

static size_t glyphs(const std::string &s) {
  size_t n = 0;
  for (char c : s)
    if (((unsigned char) c & 0xC0) != 0x80)
      n++;
  return n;
}

static std::string degrees(int32_t e4) {
  char buf[16];
  const uint32_t a = (uint32_t) (e4 < 0 ? -(int64_t) e4 : e4);
  snprintf(buf, sizeof(buf), "%s%u.%04u", e4 < 0 ? "-" : "", (unsigned) (a / 10000), (unsigned) (a % 10000));
  return buf;
}

std::string f1_met_url(int32_t lat_e4, int32_t lon_e4) {
  return "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=" + degrees(lat_e4) +
         "&lon=" + degrees(lon_e4);
}

std::string f1_met_agent(const std::string &version) {
  return "GameDayScoreboard/" + (version.empty() ? std::string("1.0") : version) +
         " github.com/ApolloAutomation/gameday-scoreboard-main";
}

uint8_t met_weather_code(const std::string &symbol) {
  const std::string s = symbol.substr(0, symbol.find('_'));
  auto has = [&s](const char *w) { return s.find(w) != std::string::npos; };
  const bool light = s.compare(0, 5, "light") == 0, heavy = s.compare(0, 5, "heavy") == 0;
  if (has("thunder"))
    return 95;
  if (s == "clearsky")
    return 0;
  if (s == "fair")
    return 1;
  if (s == "partlycloudy")
    return 2;
  if (s == "cloudy")
    return 3;
  if (s == "fog")
    return 45;
  const bool showers = has("showers");
  if (has("sleet"))
    return showers ? 85 : light ? 66 : 67;
  if (has("snow"))
    return showers ? (heavy ? 86 : 85) : light ? 71 : heavy ? 75 : 73;
  if (has("rain"))
    return showers ? (light ? 80 : heavy ? 82 : 81) : light ? 61 : heavy ? 65 : 63;
  return 255;
}

namespace detail {

void fill_met_filter(JsonDocument &f) {
  JsonObject t = f["properties"]["timeseries"].add<JsonObject>();
  t["time"] = true;
  JsonObject d = t["data"].to<JsonObject>();
  JsonObject inst = d["instant"]["details"].to<JsonObject>();
  inst["air_temperature"] = true;
  inst["wind_speed"] = true;
  JsonObject hour = d["next_1_hours"].to<JsonObject>();
  hour["summary"]["symbol_code"] = true;
  hour["details"]["precipitation_amount"] = true;
}

int met_weather_from_doc(JsonObjectConst root, int64_t now, F1Weather &out) {
  JsonArrayConst series = root["properties"]["timeseries"];
  if (series.isNull())
    return -1;
  F1Weather best;
  size_t looked = 0;
  for (JsonVariantConst e : series) {
    if (++looked > 48)
      break;
    JsonVariantConst d = e["data"], det = d["instant"]["details"], hour = d["next_1_hours"];
    const int64_t t = when(e, "time");
    if (t <= 0 || !det["air_temperature"].is<float>())
      continue;
    // The latest hour not ahead of now, else the first.
    if (best.valid && t > now)
      break;
    best.valid = true;
    best.at = t;
    best.temp_c10 = (int16_t) lround(det["air_temperature"].as<float>() * 10.0f);
    best.wind_kmh10 = (int16_t) lround((det["wind_speed"] | 0.0f) * 36.0f);
    const float mm = hour["details"]["precipitation_amount"] | 0.0f;
    best.precip_mm10 = (uint16_t) (mm > 0 && mm < 1000 ? lround(mm * 10.0f) : 0);
    best.code = met_weather_code(hour["summary"]["symbol_code"] | "");
    best.rain_pct = -1;
    if (t >= now)
      break;
  }
  if (!best.valid)
    return -1;
  out = best;
  return 1;
}

}

// By competitionType, as the racing board reads it.
static RaceKind session_kind(JsonVariantConst ct, uint8_t &number) {
  number = 0;
  if (ct.isNull())
    return RaceKind::RACE;
  JsonVariantConst idv = detail::field(ct, "id");
  const int id = idv.is<int>() ? idv.as<int>() : atoi(detail::text(ct, "id").c_str());
  switch (id) {
    case 1: {
      const std::string ab = detail::text(ct, "abbreviation");
      if (!ab.empty() && ab.back() >= '1' && ab.back() <= '9')
        number = (uint8_t) (ab.back() - '0');
      return RaceKind::PRACTICE;
    }
    case 2:
      return RaceKind::QUALIFYING;
    case 5:
      return RaceKind::SPRINT_QUALIFYING;
    case 6:
      return RaceKind::SPRINT;
    default:
      return id == 3 || id == 4 ? RaceKind::RACE : RaceKind::PRACTICE;
  }
}

static void add_session(F1Schedule &s, const F1Session &ss) {
  if (s.n >= kF1Sessions)
    return;
  size_t at = s.n++;
  for (; at > 0 && s.sessions[at - 1].start > ss.start; at--)
    s.sessions[at] = s.sessions[at - 1];
  s.sessions[at] = ss;
}

static void put_circuit(F1Schedule &s, const CircuitInfo *c) {
  if (c == nullptr)
    return;
  put(s.circuit_id, sizeof(s.circuit_id), c->jolpica);
  if (s.circuit[0] == '\0')
    put(s.circuit, sizeof(s.circuit), c->name);
  s.lat_e4 = c->lat_e4;
  s.lon_e4 = c->lon_e4;
  s.has_pos = true;
}

int f1_header_schedule(JsonObjectConst root, const F1Live &live, F1Schedule &out) {
  out = F1Schedule{};
  const GameSnapshot &g = live.snap;
  if (!g.valid || g.event_id.empty())
    return 0;
  for (JsonVariantConst e : root["sports"][0]["leagues"][0]["events"].as<JsonArrayConst>()) {
    if (detail::text(e, "id") != g.event_id)
      continue;
    F1Session ss;
    ss.start = detail::when(e, "date");
    if (ss.start <= 0)
      continue;
    ss.kind = session_kind(detail::field(e, "competitionType"), ss.number);
    add_session(out, ss);
  }
  if (out.n == 0)
    return 0;
  out.valid = true;
  put(out.race, sizeof(out.race), g.game_name);
  put(out.circuit, sizeof(out.circuit), g.venue);
  put_circuit(out, circuit_by_name(g.venue));
  int y = 0, m = 0, d = 0;
  detail::civil_from_epoch(out.sessions[out.n - 1].start, y, m, d);
  out.season = (uint16_t) y;
  return 1;
}

F1Schedule f1_calendar_schedule(const GolfNext &next) {
  F1Schedule s;
  if (!next.valid || next.start_epoch <= 0)
    return s;
  s.valid = true;
  put(s.race, sizeof(s.race), next.name);
  put_circuit(s, circuit_by_name(next.name));
  F1Session first, race;
  first.kind = RaceKind::PRACTICE;
  first.number = 1;
  first.start = next.start_epoch;
  race.start = next.end_epoch > next.start_epoch ? next.end_epoch : next.start_epoch;
  if (race.start > first.start)
    add_session(s, first);
  add_session(s, race);
  return s;
}

F1Units f1_units_for_tz(uint8_t tz) {
  F1Units u;
  u.fahrenheit = tz_fahrenheit(tz);
  u.mph = tz_miles(tz);
  return u;
}

std::string f1_cell(const std::string &left, const std::string &right, size_t chars) {
  const size_t r = glyphs(right);
  if (r == 0)
    return f1_fit(left, chars);
  if (r >= chars)
    return f1_fit(right, chars);
  const std::string l = left.empty() ? std::string() : f1_fit(left, chars - r - 1);
  return l + std::string(chars - glyphs(l) - r, ' ') + right;
}

std::string f1_age_text(int64_t s) {
  if (s < kHour)
    return std::to_string(s < 60 ? 1 : s / 60) + "m";
  if (s < 48 * kHour)
    return std::to_string(s / kHour) + "h";
  return std::to_string(s / (24 * kHour)) + "d";
}

bool F1Host::fail(int64_t now, bool refused, uint32_t retry_after_ms, uint32_t rnd, bool failover) {
  if (this->fails < 255)
    this->fails++;
  if (refused)
    this->refused = true;
  this->wait_until = now + backoff_ms(this->fails, this->refused, retry_after_ms, rnd) / 1000;
  if (!failover || (!refused && this->fails < kF1FailOver))
    return false;
  // Back to it after 30 minutes, doubling to 6 hours.
  int64_t out = 30 * 60;
  for (uint8_t i = 0; i < this->outs && out < 6 * kHour; i++)
    out *= 2;
  if (out > 6 * kHour)
    out = 6 * kHour;
  const bool first = this->outs == 0;
  if (this->outs < 255)
    this->outs++;
  this->out_until = now + (this->wait_until - now > out ? this->wait_until - now : out);
  return first;
}

// ---- the app ---------------------------------------------------------------------

static int64_t race_start_of(const F1Schedule &s) {
  for (size_t i = s.n; i > 0; i--)
    if (s.sessions[i - 1].kind == RaceKind::RACE)
      return s.sessions[i - 1].start;
  return s.n > 0 ? s.sessions[s.n - 1].start : 0;
}

// Its race over three hours ago, or none.
static bool weekend_over(const F1Schedule &s, int64_t now) {
  return !s.valid || s.n == 0 || now > race_start_of(s) + 3 * kHour;
}

static bool race_like(RaceKind k) { return k == RaceKind::RACE || k == RaceKind::SPRINT; }

static F1Units units() { return f1_units_for_tz(app_env().tz); }

F1App::F1App() = default;

const char *const *F1App::hosts() const {
  // Not espn.com: that would take Game Day's reads.
  static const char *const kList[] = {"api.jolpi.ca", "api.open-meteo.com", "api.met.no", nullptr};
  return kList;
}

bool F1App::jolpica_out_() const { return this->host_[HOST_JOLPICA].backup(); }

const F1Schedule &F1App::schedule(int64_t now) const {
  if (this->sched_.valid && !this->jolpica_out_())
    return this->sched_;
  if (!weekend_over(this->head_sched_, now))
    return this->head_sched_;
  if (!weekend_over(this->cal_sched_, now))
    return this->cal_sched_;
  if (this->sched_.valid)
    return this->sched_;
  return this->head_sched_.valid ? this->head_sched_ : this->cal_sched_;
}

const CircuitInfo *F1App::circuit(int64_t now) const {
  const F1Schedule &s = this->schedule(now);
  const CircuitInfo *c = s.valid ? circuit_for(s) : nullptr;
  if (c == nullptr && !this->live_.snap.venue.empty())
    c = circuit_by_name(this->live_.snap.venue);
  return c;
}

bool F1App::weather_place_(int64_t now, int32_t &lat, int32_t &lon) const {
  const F1Schedule &s = this->schedule(now);
  if (s.valid && s.has_pos) {
    lat = s.lat_e4;
    lon = s.lon_e4;
    return true;
  }
  const CircuitInfo *c = this->circuit(now);
  if (c == nullptr)
    return false;
  lat = c->lat_e4;
  lon = c->lon_e4;
  return true;
}

std::string F1App::place_(int64_t now) const {
  const F1Schedule &s = this->schedule(now);
  if (s.valid && s.locality[0] != '\0')
    return s.locality;
  const CircuitInfo *c = this->circuit(now);
  return c != nullptr ? std::string(c->name) : std::string(s.circuit);
}

bool F1App::fresh_weather_(int64_t now) const {
  int32_t lat, lon;
  return this->weather_.valid && this->ok_[(int) Read::WEATHER] != 0 &&
         now - this->ok_[(int) Read::WEATHER] < 3 * kHour && this->weather_place_(now, lat, lon) &&
         lat == this->weather_lat_ && lon == this->weather_lon_;
}

bool F1App::stale_standings_(int64_t now) const {
  const int64_t at = this->ok_[(int) Read::DRIVERS];
  return this->jolpica_out_() || (at != 0 && now - at > 12 * kHour);
}

bool F1App::live_card_(int64_t now) const {
  const Racing &r = this->live_.snap.race;
  return this->ok_[(int) Read::HEADER] != 0 && f1_live_has_board(this->live_) &&
         (r.phase == RacePhase::LIVE || (r.session_start != 0 && now - r.session_start < 24 * kHour));
}

int64_t F1App::due_(Read r, int64_t now) const {
  const int i = (int) r;
  // From the last good read; failures wait on hosts.
  auto after = [this, i](uint32_t ms) { return this->ok_[i] == 0 ? 0 : this->ok_[i] + (int64_t) (ms / 1000); };
  auto later = [](int64_t a, int64_t b) { return a > b ? a : b; };
  const F1Host &espn = this->host_[HOST_ESPN], &jolpica = this->host_[HOST_JOLPICA];
  switch (r) {
    case Read::HEADER: {
      uint32_t ms = f1_header_poll_ms(this->live_, now);
      if (racing_on_track(this->live_.snap, now) && ms > 30000)
        ms = 30000;
      return later(after(ms), espn.wait_until);
    }
    case Read::NEXT:
    case Read::DRIVERS:
    case Read::CONSTRUCTORS:
    case Read::RESULTS: {
      static const F1Read kRead[] = {F1Read::NEXT, F1Read::DRIVERS, F1Read::CONSTRUCTORS, F1Read::LAST_RESULTS};
      // Off season: the header's last weekend, not none.
      const F1Schedule &s = r == Read::NEXT ? this->sched_ : this->schedule(now);
      int64_t at = after(f1_poll_ms(kRead[i - 1], s, now));
      if (jolpica.backup())
        at = later(at, jolpica.out_until);
      return later(at, jolpica.wait_until);
    }
    case Read::CIRCUIT: {
      const CircuitInfo *c = this->circuit(now);
      if (c == nullptr || c->espn_id == 0 || (this->facts_.valid && this->facts_.espn_id == c->espn_id))
        return kNever;
      const bool tried = this->facts_try_ == c->espn_id;
      return later(tried ? this->last_[i] + 30 * 60 : 0, espn.wait_until);
    }
    case Read::ATHLETE: {
      const uint32_t id = this->facts_.record_athlete;
      if (!this->facts_.valid || id == 0 || this->facts_.record_code[0] != '\0')
        return kNever;
      return later(this->athlete_try_ == id ? this->last_[i] + 30 * 60 : 0, espn.wait_until);
    }
    case Read::WEATHER: {
      int32_t lat, lon;
      // Shown only before the weekend ends.
      if (weekend_over(this->schedule(now), now) || !this->weather_place_(now, lat, lon))
        return kNever;
      const F1Host &om = this->host_[HOST_OPEN_METEO];
      const F1Host &h = om.backup() && now < om.out_until ? this->host_[HOST_MET] : om;
      const bool moved = lat != this->weather_lat_ || lon != this->weather_lon_;
      const uint32_t ms = f1_race_weekend(this->schedule(now), now) ? 15 * 60000 : 60 * 60000;
      return later(moved ? 0 : after(ms), h.wait_until);
    }
    case Read::CALENDAR:
      // Jolpica out and the header's weekend over.
      if (!this->jolpica_out_() || !weekend_over(this->head_sched_, now))
        return kNever;
      return later(this->last_[i] == 0 ? 0 : this->last_[i] + 6 * kHour, espn.wait_until);
    default:
      return kNever;
  }
}

F1App::Read F1App::next_read(int64_t now, int64_t &at) const {
  static const Read kOrder[] = {Read::HEADER,  Read::NEXT,    Read::DRIVERS,      Read::WEATHER, Read::CIRCUIT,
                                Read::ATHLETE, Read::RESULTS, Read::CONSTRUCTORS, Read::CALENDAR};
  Read best = Read::NONE;
  at = kNever;
  for (Read r : kOrder) {
    const int64_t d = this->due_(r, now);
    if (d <= now) {
      at = d;
      return r;
    }
    if (d < at) {
      at = d;
      best = r;
    }
  }
  return best;
}

uint32_t F1App::poll_ms(int64_t now) const {
  int64_t at;
  if (this->next_read(now, at) == Read::NONE)
    return 6 * 3600 * 1000;
  if (at <= now || at <= this->last_any_)
    return 0;
  const int64_t s = at - this->last_any_;
  return s > 24 * kHour ? 24 * 3600 * 1000 : (uint32_t) (s * 1000);
}

bool F1App::request(int64_t now, AppRequest &out) {
  // A read given up on counts as failed.
  if (this->inflight_ != Read::NONE) {
    this->result_ = AppResult{};
    this->fail_(this->inflight_host_, now);
    this->inflight_ = Read::NONE;
  }
  int64_t at;
  const Read r = this->next_read(now, at);
  if (r == Read::NONE || at > now)
    return false;
  out = AppRequest{};
  Host host = HOST_ESPN;
  switch (r) {
    case Read::HEADER:
      out.url = header_url(League::F1);
      break;
    case Read::NEXT:
    case Read::DRIVERS:
    case Read::CONSTRUCTORS:
    case Read::RESULTS: {
      static const F1Read kRead[] = {F1Read::NEXT, F1Read::DRIVERS, F1Read::CONSTRUCTORS, F1Read::LAST_RESULTS};
      out.url = f1_jolpica_url(kRead[(int) r - 1]);
      out.headers.push_back({"User-Agent", kF1UserAgent});
      host = HOST_JOLPICA;
      break;
    }
    case Read::CIRCUIT:
      this->facts_try_ = this->circuit(now)->espn_id;
      out.url = f1_circuit_url(this->facts_try_);
      break;
    case Read::ATHLETE:
      this->athlete_try_ = this->facts_.record_athlete;
      out.url = f1_athlete_url(this->athlete_try_);
      break;
    case Read::WEATHER: {
      this->weather_place_(now, this->req_lat_, this->req_lon_);
      const F1Host &om = this->host_[HOST_OPEN_METEO];
      if (om.backup() && now < om.out_until) {
        out.url = f1_met_url(this->req_lat_, this->req_lon_);
        out.headers.push_back({"User-Agent", f1_met_agent(app_env().version)});
        host = HOST_MET;
      } else {
        out.url = f1_weather_url(this->req_lat_, this->req_lon_);
        out.headers.push_back({"User-Agent", kF1UserAgent});
        host = HOST_OPEN_METEO;
      }
      break;
    }
    case Read::CALENDAR:
      out.url = golf_calendar_url(League::F1, now, false);
      this->cal_after_ = this->live_.snap.event_id;
      this->cal_after_start_ = this->live_.snap.kickoff_epoch;
      break;
    default:
      return false;
  }
  this->inflight_ = r;
  this->inflight_host_ = host;
  this->result_ = AppResult{};
  this->last_[(int) r] = now;
  this->last_any_ = now;
  return true;
}

bool F1App::filter(JsonDocument &f) {
  switch (this->inflight_) {
    case Read::HEADER:
      detail::fill_f1_live_filter(f);
      return true;
    case Read::NEXT:
      detail::fill_f1_race_filter(f);
      return true;
    case Read::DRIVERS:
    case Read::CONSTRUCTORS:
      detail::fill_f1_standings_filter(f, this->inflight_ == Read::DRIVERS);
      return true;
    case Read::RESULTS:
      detail::fill_f1_results_filter(f);
      return true;
    case Read::CIRCUIT:
      detail::fill_f1_circuit_filter(f);
      return true;
    case Read::ATHLETE:
      detail::fill_f1_athlete_filter(f);
      return true;
    case Read::WEATHER:
      if (this->inflight_host_ == HOST_MET)
        detail::fill_met_filter(f);
      else
        detail::fill_f1_weather_filter(f);
      return true;
    case Read::CALENDAR:
      detail::fill_racing_calendar_filter(f);
      return true;
    default:
      return false;
  }
}

bool F1App::parse_doc(JsonObjectConst root, int64_t now) {
  int found = -1;
  switch (this->inflight_) {
    case Read::HEADER:
      this->p_live_ = F1Live{};
      found = detail::f1_live_from_doc(root, this->p_live_);
      f1_header_schedule(root, this->p_live_, this->p_sched_);
      break;
    case Read::NEXT:
      this->p_sched_ = F1Schedule{};
      found = detail::f1_race_from_doc(root, this->p_sched_);
      break;
    case Read::DRIVERS:
    case Read::CONSTRUCTORS:
      this->p_stand_ = F1Standings{};
      found = detail::f1_standings_from_doc(root, this->inflight_ == Read::DRIVERS, this->p_stand_);
      break;
    case Read::RESULTS:
      this->p_results_ = F1Results{};
      found = detail::f1_results_from_doc(root, this->p_results_);
      break;
    case Read::CIRCUIT:
      this->p_facts_ = F1CircuitFacts{};
      found = detail::f1_circuit_from_doc(root, this->p_facts_) == 1 ? 1 : -1;
      break;
    case Read::ATHLETE:
      this->p_athlete_ = F1Athlete{};
      found = detail::f1_athlete_from_doc(root, this->p_athlete_) == 1 ? 1 : -1;
      break;
    case Read::WEATHER:
      this->p_weather_ = F1Weather{};
      found = this->inflight_host_ == HOST_MET ? detail::met_weather_from_doc(root, now, this->p_weather_)
                                               : detail::f1_weather_from_doc(root, this->p_weather_);
      found = found == 1 ? 1 : -1;
      break;
    case Read::CALENDAR:
      this->p_next_ = GolfNext{};
      found = detail::racing_next_from_doc(root, now, this->cal_after_, this->cal_after_start_, this->p_next_);
      break;
    default:
      break;
  }
  return found >= 0;
}

void F1App::fail_(Host h, int64_t now) {
  this->rnd_ = this->rnd_ * 1664525u + 1013904223u;
  const bool failover = h == HOST_JOLPICA || h == HOST_OPEN_METEO;
  this->host_[h].fail(now, this->result_.refused, this->result_.retry_after_ms, this->rnd_ ^ (uint32_t) now, failover);
}

void F1App::apply(bool ok, int64_t now) {
  const Read r = this->inflight_;
  const Host h = this->inflight_host_;
  this->inflight_ = Read::NONE;
  if (r == Read::NONE)
    return;
  if (!ok) {
    this->fail_(h, now);
    this->result_ = AppResult{};
    return;
  }
  this->result_ = AppResult{};
  this->host_[h].ok();
  this->ok_[(int) r] = now;
  switch (r) {
    case Read::HEADER:
      // An edge's older copy: kept out, twice at most.
      if (racing_read_is_older(this->live_.snap, this->p_live_.snap) && this->stale_reads_ < 2) {
        this->stale_reads_++;
        break;
      }
      this->stale_reads_ = 0;
      this->live_ = this->p_live_;
      this->head_sched_ = this->p_sched_;
      f1_live_codes(this->live_, this->drivers_);
      break;
    case Read::NEXT:
      this->sched_ = this->p_sched_;
      break;
    case Read::DRIVERS:
      this->drivers_ = this->p_stand_;
      f1_live_codes(this->live_, this->drivers_);
      break;
    case Read::CONSTRUCTORS:
      this->teams_ = this->p_stand_;
      break;
    case Read::RESULTS:
      this->results_ = this->p_results_;
      break;
    case Read::CIRCUIT: {
      F1CircuitFacts f = this->p_facts_;
      f.espn_id = this->facts_try_;  // the circuit asked for
      if (f.record_athlete != 0 && f.record_athlete == this->facts_.record_athlete) {
        memcpy(f.record_code, this->facts_.record_code, sizeof(f.record_code));
        memcpy(f.record_name, this->facts_.record_name, sizeof(f.record_name));
      }
      this->facts_ = f;
      break;
    }
    case Read::ATHLETE:
      if (this->facts_.record_athlete == this->athlete_try_) {
        const F1Athlete &x = this->p_athlete_;
        put(this->facts_.record_code, sizeof(this->facts_.record_code), x.code[0] != '\0' ? x.code : f1_code(x.name));
        put(this->facts_.record_name, sizeof(this->facts_.record_name), x.name);
      }
      break;
    case Read::WEATHER:
      this->weather_ = this->p_weather_;
      this->weather_lat_ = this->req_lat_;
      this->weather_lon_ = this->req_lon_;
      this->weather_backup_ = h == HOST_MET;
      break;
    case Read::CALENDAR:
      this->cal_sched_ = f1_calendar_schedule(this->p_next_);
      break;
    default:
      break;
  }
}

bool F1App::busy(int64_t now) const {
  const int64_t heard = this->ok_[(int) Read::HEADER];
  const bool fresh = heard != 0 && now - heard < 15 * 60;
  if (fresh && racing_on_track(this->live_.snap, now))
    return true;
  const F1Schedule &s = this->schedule(now);
  const Racing &r = this->live_.snap.race;
  for (size_t i = 0; i < s.n; i++) {
    const F1Session &ss = s.sessions[i];
    const int64_t len = ss.kind == RaceKind::RACE ? 3 * kHour : 90 * 60;
    if (!ss.timed || now < ss.start - 10 * 60 || now >= ss.start + len)
      continue;
    // The header saw it end: no longer busy.
    if (fresh && r.session_start == ss.start && (r.phase == RacePhase::BREAK || r.phase == RacePhase::FINAL))
      continue;
    return true;
  }
  return false;
}

void F1App::state(JsonObject out, int64_t /*now*/) const {
  out["schedule"] = this->sched_.valid && !this->jolpica_out_() ? "jolpica" : "espn";
  out["weather"] = this->weather_backup_ ? "met-norway" : "open-meteo";
  if (this->ok_[(int) Read::DRIVERS] != 0)
    out["standings_at"] = this->ok_[(int) Read::DRIVERS];
}

// ---- screens ---------------------------------------------------------------------

size_t F1App::rotation(int64_t now, F1Screen *out, size_t cap) const {
  F1Have h;
  h.live = this->live_card_(now);
  h.on_track = racing_on_track(this->live_.snap, now);
  const F1Schedule &s = this->schedule(now);
  h.next = s.valid && s.n > 0 && !weekend_over(s, now);
  h.circuit = h.next && this->circuit(now) != nullptr;
  h.weather = h.next && this->fresh_weather_(now);
  h.drivers = this->drivers_.valid && this->drivers_.n > 0;
  h.constructors = this->teams_.valid && this->teams_.n > 0;
  h.last_race = this->results_.valid && this->results_.n > 0;
  return f1_rotation(h, out, cap);
}

static uint32_t card_s(F1Screen s) { return s == F1Screen::WEATHER ? 8 : 10; }

F1Screen F1App::screen(int64_t now, uint32_t *left) const {
  F1Screen list[8];
  const size_t n = this->rotation(now, list, 8);
  if (left != nullptr)
    *left = 0;
  if (n <= 1)
    return n == 1 ? list[0] : F1Screen::LIVE;
  uint32_t total = 0;
  for (size_t i = 0; i < n; i++)
    total += card_s(list[i]);
  uint32_t t = (uint32_t) ((uint64_t) (now < 0 ? 0 : now) % total);
  for (size_t i = 0; i < n; i++) {
    if (t < card_s(list[i])) {
      if (left != nullptr)
        *left = card_s(list[i]) - t;
      return list[i];
    }
    t -= card_s(list[i]);
  }
  return list[0];
}

static void add_row(AppFrame &f, const std::string &pos, const std::string &name, const std::string &value,
                    const std::string &detail, uint32_t name_color, uint32_t value_color = kGray) {
  BoardLines &b = f.rows;
  b.pos.push_back(pos);
  b.name.push_back(name);
  b.value.push_back(value);
  b.detail.push_back(detail);
  b.name_color.push_back((int) name_color);
  b.value_color.push_back((int) value_color);
}

std::string f1_short_name(const std::string &name, size_t max) {
  if (glyphs(name) <= max)
    return name;
  const size_t dash = name.find('-');
  if (name.find(' ') == std::string::npos && dash != std::string::npos && dash <= max)
    return name.substr(0, dash);
  return detail::whole_words(name, max);
}

// The status cell; 12 centered characters on one panel.
static void put_status(AppFrame &out, int w, const std::string &status, uint32_t color) {
  out.rows.status = f1_fit(status, w < 128 ? 12 : (size_t) (w - 2) / 5);
  out.rows.status_color = color;
}

// What the status leaves the title; none on one panel.
static size_t title_room(const AppFrame &out, int w) {
  if (w < 128)
    return 0;
  const size_t chars = (size_t) (w - 2) / 5, used = glyphs(out.rows.status);
  return used == 0 ? chars : used + 1 < chars ? chars - used - 1 : 0;
}

static void put_title(AppFrame &out, int w, const std::string &title) {
  const size_t room = title_room(out, w);
  out.rows.title = room > 0 ? f1_fit(title, room) : std::string();
}

static uint32_t car_color(const F1Car &c) {
  return c.rgb != 0 ? esphome::gameday::lift_color(c.rgb, 0xC0) : f1_team_color(c.team);
}

void F1App::live_frame_(int w, int64_t now, AppFrame &out) const {
  const GameSnapshot &g = this->live_.snap;
  std::string when;
  if (g.race.next_start > 0)
    when = racing_when(app_local(g.race.next_start), app_local(now));
  put_status(out, w, racing_state_text(g), racing_state_color(g));
  put_title(out, w, racing_event_name(g.game_name, title_room(out, w)));
  out.ticker = racing_status_text(g, TickerOptions{}, when);
  // The flag first, then names in team colors.
  if (g.race.phase == RacePhase::LIVE && g.race.flag == 'R')
    out.spans.push_back({"red flag", kRed});
  else if (g.race.phase == RacePhase::LIVE && g.race.flag == 'Y')
    out.spans.push_back({"yellow flag", kAmber});
  for (size_t i = 0; i < g.race.rows.size() && i < kRaceRows; i++)
    out.spans.push_back({g.race.rows[i].name, car_color(this->live_.cars[i])});
  const bool wide = w >= 128, race = race_like(g.race.kind);
  out.rows.value_narrow = wide;
  for (size_t i = 0; i < g.race.rows.size() && i < kRaceRows && i < kF1Rows; i++) {
    const RaceRow &rr = g.race.rows[i];
    const F1Car &car = this->live_.cars[i];
    std::string detail = racing_detail_text(rr);
    if (detail.empty() && i == 0 && race)
      detail = g.race.phase == RacePhase::FINAL ? "Winner" : "Leader";
    if (wide) {
      std::string extra = race ? (car.has_pits ? std::to_string(car.pits) : "")
                               : (car.laps > 0 ? std::to_string(car.laps) : "");
      add_row(out, racing_pos_text(rr), racing_name_fit(rr.name, kNameWide), f1_fit(extra, kValue), detail,
              car_color(car));
    } else {
      const std::string code = car.code[0] != '\0' ? std::string(car.code) : f1_code(rr.name);
      add_row(out, racing_pos_text(rr), f1_cell(code, detail, kCellNarrow), "", "", car_color(car));
    }
  }
}

static std::string session_tag(const F1Session &s, bool shortest) {
  switch (s.kind) {
    case RaceKind::PRACTICE:
      return "FP" + (s.number > 0 ? std::to_string(s.number) : std::string());
    case RaceKind::QUALIFYING:
      return shortest ? "Q" : "Qual";
    case RaceKind::SPRINT_QUALIFYING:
      return "SQ";
    case RaceKind::SPRINT:
      return "Spr";
    default:
      return shortest ? "GP" : "Race";
  }
}

static std::string month_day(const struct tm &t) {
  if (t.tm_mon < 0 || t.tm_mon > 11)
    return "";
  return std::string(kMonths[t.tm_mon]) + " " + std::to_string(t.tm_mday);
}

void F1App::next_frame_(int w, int64_t now, AppFrame &out) const {
  const F1Schedule &s = this->schedule(now);
  const struct tm nl = app_local(now);
  const bool wide = w >= 128;
  int next = -1;
  for (size_t i = 0; i < s.n && next < 0; i++)
    if (s.sessions[i].start > now)
      next = (int) i;
  if (next >= 0) {
    const F1Session &ns = s.sessions[next];
    const int64_t until = ns.start - now;
    // A date alone: its day, not a countdown.
    put_status(out, w, ns.timed ? f1_countdown(until, wide ? 7 : 12) : day_label(app_local(ns.start), nl),
               until <= kHour ? kAmber : kWhite);
  } else {
    put_status(out, w, "Race on", kGold);
  }
  put_title(out, w, racing_event_name(s.race, title_room(out, w)));
  out.rows.value_narrow = wide;
  for (size_t i = 0; i < s.n && i < kF1Rows; i++) {
    const F1Session &ss = s.sessions[i];
    const struct tm lt = app_local(ss.start);
    const std::string day = lt.tm_wday >= 0 && lt.tm_wday < 7 ? kDays[lt.tm_wday] : "";
    const std::string at = ss.timed ? golf_tee_label(lt) : month_day(lt);
    const bool past = ss.start <= now;
    const uint32_t color = past ? kDim : (int) i == next ? kGold : kWhite;
    const uint32_t value_color = past ? kDim : kGray;
    if (wide) {
      std::string label = racing_session_long(ss.kind, ss.number);
      if (glyphs(label) > kNameWide)
        label = racing_session_label(ss.kind, ss.number);
      add_row(out, "", f1_fit(label, kNameWide), day, f1_fit(at, kDetail), color, value_color);
    } else {
      std::string tag = session_tag(ss, false);
      if (glyphs(tag) + 1 + glyphs(at) > kCellNarrow)
        tag = session_tag(ss, true);
      add_row(out, day.substr(0, 2), f1_cell(tag, at, kCellNarrow), "", "", color, value_color);
    }
  }
}

void F1App::circuit_frame_(int w, int h, int64_t now, AppFrame &out) const {
  const CircuitInfo *c = this->circuit(now);
  static const F1CircuitFacts kNone;
  const F1CircuitFacts &facts =
      c != nullptr && this->facts_.valid && this->facts_.espn_id == c->espn_id ? this->facts_ : kNone;
  const F1Units u = units();
  const bool wide = w >= 128;
  const std::string full = facts.valid ? std::string(facts.name) : c != nullptr ? std::string(c->name) : "";
  const std::string brief = c != nullptr ? std::string(c->name) : full;
  if (wide) {
    const size_t room = title_room(out, w);
    out.rows.title = glyphs(full) <= room ? full : glyphs(brief) <= room ? brief : detail::whole_words(full, room);
  } else {
    put_status(out, w, glyphs(full) <= 12 ? full : f1_short_name(brief, 12), kWhite);
  }
  const esphome::gameday::RowsGeom g = esphome::gameday::rows_geom(w);
  // Right of the facts; one panel's whole board.
  const int x0 = wide ? g.name.x + g.name.w + 3 : 2, x1 = wide ? w - 2 : w - 3;
  const int y0 = 9, y1 = (h > 20 ? h : 64) - 10;
  if (c != nullptr) {
    F1Point pts[kF1ShapeMax];
    const size_t n = f1_shape(*c, (int16_t) x0, (int16_t) y0, (int16_t) (x1 - x0 + 1), (int16_t) (y1 - y0 + 1), pts,
                              kF1ShapeMax);
    std::vector<AppPoint> line;
    line.reserve(n);
    for (size_t i = 0; i < n; i++)
      line.push_back(AppPoint{pts[i].x, pts[i].y});
    out.line.pts = app_line_thin(line, kAppLinePoints);
    out.line.color = kWhite;
  }
  if (!wide)
    return;
  const uint32_t len = facts.length_m > 0 ? facts.length_m : c != nullptr ? c->length_m : 0;
  const unsigned laps = facts.laps > 0 ? facts.laps : c != nullptr ? c->laps : 0;
  const unsigned turns = facts.turns > 0 ? facts.turns : c != nullptr ? c->turns : 0;
  if (len > 0)
    add_row(out, "", f1_fit(f1_length_text(len, u), kNameWide), "", "", kWhite);
  if (laps > 0)
    add_row(out, "", std::to_string(laps) + " laps", "", "", kWhite);
  if (turns > 0)
    add_row(out, "", std::to_string(turns) + " turns", "", "", kWhite);
  std::string rec = facts.record;
  if (!rec.empty()) {
    while (glyphs("Rec " + rec) > kNameWide && rec.size() > 1)
      rec.pop_back();
    add_row(out, "", "Rec " + rec, "", "", kWhite);
    const std::string year = facts.record_year > 0 ? " " + std::to_string(facts.record_year) : "";
    std::string by = std::string(facts.record_name) + year;
    if (facts.record_name[0] == '\0' || glyphs(by) > kNameWide)
      by = (facts.record_code[0] != '\0' ? std::string(facts.record_code) : f1_code(facts.record_name)) + year;
    if (glyphs(by) > glyphs(year) + 1)
      add_row(out, "", f1_fit(by, kNameWide), "", "", kGray);
  }
}

// "10 km/h", then "10km/h", then "10kph" to fit.
static std::string squeeze(std::string s, size_t max) {
  if (glyphs(s) <= max)
    return s;
  const size_t sp = s.find(' ');
  if (sp != std::string::npos)
    s.erase(sp, 1);
  const size_t kmh = s.find("km/h");
  if (glyphs(s) > max && kmh != std::string::npos)
    s.replace(kmh, 4, "kph");
  return f1_fit(s, max);
}

static std::string rain_text(uint16_t mm10, const F1Units &u) {
  char buf[24];
  if (u.mph)
    snprintf(buf, sizeof(buf), "%.2f in", mm10 / 254.0);
  else
    snprintf(buf, sizeof(buf), "%u.%u mm", (unsigned) (mm10 / 10), (unsigned) (mm10 % 10));
  return buf;
}

void F1App::weather_frame_(int w, int64_t now, AppFrame &out) const {
  const F1Weather &x = this->weather_;
  const F1Units u = units();
  const bool wide = w >= 128;
  out.rows.value_narrow = wide;
  const std::string temp = f1_temp_text(x.temp_c10, u), word = f1_weather_word(x.code);
  if (wide) {
    put_status(out, w, word, f1_weather_color(x.code));
    out.rows.title = detail::whole_words(this->place_(now), title_room(out, w));
  } else {
    put_status(out, w, glyphs(temp) + 1 + glyphs(word) <= 12 && !word.empty() ? temp + " " + word : temp, kWhite);
  }
  const std::string pct = x.rain_pct >= 0 ? std::to_string(x.rain_pct) + "%" : "";
  if (wide) {
    std::string sky = f1_weather_long(x.code);
    if (glyphs(sky) > kNameWide)
      sky = f1_weather_word(x.code);
    if (!sky.empty())
      add_row(out, "", sky, "", "", f1_weather_color(x.code));
    add_row(out, "", "Air temp", "", temp, kWhite);
    if (!pct.empty())
      add_row(out, "", "Rain chance", "", pct, kWhite);
    add_row(out, "", "Wind", "", squeeze(f1_wind_text(x.wind_kmh10, u), kDetail), kWhite);
    if (x.precip_mm10 > 0)
      add_row(out, "", "Rain now", "", squeeze(rain_text(x.precip_mm10, u), kDetail), kWhite);
    return;
  }
  add_row(out, "", f1_cell("Air", temp, kCellNarrow), "", "", kWhite);
  if (!pct.empty())
    add_row(out, "", f1_cell("Rain", pct, kCellNarrow), "", "", kWhite);
  add_row(out, "", f1_cell("Wind", squeeze(f1_wind_text(x.wind_kmh10, u), 5), kCellNarrow), "", "", kWhite);
  if (x.precip_mm10 > 0)
    add_row(out, "", f1_cell("Wet", squeeze(rain_text(x.precip_mm10, u), 6), kCellNarrow), "", "", kWhite);
}

// A team in max: "Aston Martin" is "AstonMartin".
static std::string team_fit(const F1Standing &st, size_t max) {
  const F1Team *t = f1_team(st.team);
  std::string name = t != nullptr ? t->name : st.name;
  if (glyphs(name) <= max)
    return name;
  std::string joined;
  for (char c : name)
    if (c != ' ')
      joined += c;
  if (glyphs(joined) <= max)
    return joined;
  return t != nullptr ? t->code : f1_fit(name, max);
}

static std::string points_fit(int32_t p10, size_t max) {
  std::string p = f1_points_text(p10);
  return glyphs(p) <= max ? p : std::to_string(p10 / 10);
}

void F1App::standings_frame_(const F1Standings &s, int w, int64_t now, AppFrame &out) const {
  const bool wide = w >= 128;
  out.rows.value_narrow = wide;
  const int64_t at = this->ok_[(int) (s.drivers ? Read::DRIVERS : Read::CONSTRUCTORS)];
  const char *what = s.drivers ? "Drivers" : "Teams";
  if (this->stale_standings_(now) && at != 0) {
    const std::string age = f1_age_text(now - at);
    put_status(out, w, wide ? age + " ago" : std::string(what) + " " + age, kAmber);
  } else {
    put_status(out, w, !wide ? std::string(what) : s.round > 0 ? "Round " + std::to_string(s.round) : "", wide ? kGray : kWhite);
  }
  if (wide) {
    std::string t = w >= 256 ? std::string(s.drivers ? "Driver standings" : "Constructor standings") : what;
    if (w >= 256 && s.season > 0)
      t = std::to_string(s.season) + " " + t;
    out.rows.title = detail::whole_words(t, title_room(out, w));
  }
  for (size_t i = 0; i < s.n && i < kF1Rows; i++) {
    const F1Standing &st = s.rows[i];
    const F1Team *team = f1_team(st.team);
    const std::string pos = st.pos > 0 && st.pos < 100 ? std::to_string(st.pos) : "-";
    const std::string pts = points_fit(st.points10, kDetail);
    if (wide) {
      const std::string name = s.drivers ? racing_name_fit(st.name, kNameWide) : team_fit(st, kNameWide);
      const std::string wins = st.wins > 0 ? std::to_string(st.wins) + "W" : "";
      add_row(out, pos, name, f1_fit(wins, kValue), pts, f1_team_color(st.team));
    } else {
      std::string code = s.drivers ? std::string(st.code) : team != nullptr ? team->code : "";
      if (code.empty())
        code = f1_code(st.name);
      add_row(out, pos, f1_cell(code, pts, kCellNarrow), "", "", f1_team_color(st.team));
    }
  }
}

void F1App::results_frame_(int w, AppFrame &out) const {
  const F1Results &res = this->results_;
  put_status(out, w, "Last race", kGold);
  put_title(out, w, racing_event_name(res.race, title_room(out, w)));
  const bool wide = w >= 128;
  out.rows.value_narrow = wide;
  for (size_t i = 0; i < res.n && i < kF1Rows; i++) {
    const F1Result &x = res.rows[i];
    const std::string pos = x.pos > 0 && x.pos < 100 ? std::to_string(x.pos) : "-";
    const std::string detail = f1_result_detail(x, res.laps, kDetail);
    if (wide) {
      const std::string pts = x.points10 > 0 ? points_fit(x.points10, kValue) : "";
      add_row(out, pos, racing_name_fit(x.name, kNameWide), pts, detail, f1_team_color(x.team));
    } else {
      const std::string code = x.code[0] != '\0' ? std::string(x.code) : f1_code(x.name);
      add_row(out, pos, f1_cell(code, detail, kCellNarrow), "", "", f1_team_color(x.team));
    }
  }
}

// One line for the whole rotation, so it scrolls through.
std::string F1App::ticker_(int64_t now, std::vector<TickerSpan> &spans) const {
  std::string t;
  const F1Schedule &s = this->schedule(now);
  const struct tm nl = app_local(now);
  const F1Units u = units();
  const bool weekend = s.valid && !weekend_over(s, now);
  if (weekend) {
    add_part(t, s.race);
    std::string where = s.circuit;
    for (const char *p : {s.locality, s.country})
      if (p[0] != '\0')
        where += std::string(where.empty() ? "" : ", ") + p;
    add_part(t, where);
    for (size_t i = 0; i < s.n; i++) {
      const F1Session &ss = s.sessions[i];
      if (ss.start <= now)
        continue;
      const struct tm lt = app_local(ss.start);
      add_part(t, racing_session_long(ss.kind, ss.number) + " " +
                      (ss.timed ? racing_when(lt, nl) : day_label(lt, nl)));
    }
  }
  if (weekend && this->fresh_weather_(now)) {
    const F1Weather &x = this->weather_;
    const std::string sky = f1_weather_long(x.code);
    std::string w = "Weather at " + this->place_(now) + ": " + (sky.empty() ? "" : sky + ", ") +
                    f1_temp_text(x.temp_c10, u) + ", wind " + f1_wind_text(x.wind_kmh10, u);
    if (x.rain_pct >= 0)
      w += ", " + std::to_string(x.rain_pct) + "% chance of rain";
    add_part(t, w);
  }
  auto standings = [this, now, &t, &spans](const F1Standings &st, const char *label, bool drivers) {
    if (!st.valid || st.n == 0)
      return;
    std::string line = label;
    for (size_t i = 0; i < st.n && i < 3; i++) {
      const F1Standing &r = st.rows[i];
      const F1Team *team = f1_team(r.team);
      const std::string name = drivers || team == nullptr ? std::string(r.name) : std::string(team->name);
      line += (i == 0 ? " " : ", ") + std::to_string(r.pos) + " " + name + " " + f1_points_text(r.points10);
      spans.push_back({name, f1_team_color(r.team)});
    }
    const int64_t at = this->ok_[(int) (drivers ? Read::DRIVERS : Read::CONSTRUCTORS)];
    if (this->stale_standings_(now) && at != 0)
      line += " (" + f1_age_text(now - at) + " ago)";
    add_part(t, line);
  };
  standings(this->drivers_, "Drivers:", true);
  standings(this->teams_, "Constructors:", false);
  if (this->results_.valid && this->results_.n > 0) {
    std::string line = "Last race, " + racing_event_name(this->results_.race, 24) + ":";
    for (size_t i = 0; i < this->results_.n && i < 3; i++) {
      const F1Result &x = this->results_.rows[i];
      line += (i == 0 ? " " : ", ") + std::to_string(x.pos) + " " + x.name;
      spans.push_back({x.name, f1_team_color(x.team)});
    }
    add_part(t, line);
  }
  const CircuitInfo *c = weekend ? this->circuit(now) : nullptr;
  if (c != nullptr) {
    const bool own = this->facts_.valid && this->facts_.espn_id == c->espn_id;
    const uint32_t len = own && this->facts_.length_m > 0 ? this->facts_.length_m : c->length_m;
    std::string line = std::string(own && this->facts_.name[0] != '\0' ? this->facts_.name : c->name) + ": " +
                       f1_length_text(len, u);
    if (c->laps > 0)
      line += ", " + std::to_string(own && this->facts_.laps > 0 ? this->facts_.laps : c->laps) + " laps";
    if (c->turns > 0)
      line += ", " + std::to_string(c->turns) + " turns";
    if (own && this->facts_.record[0] != '\0') {
      line += ", lap record " + std::string(this->facts_.record);
      if (this->facts_.record_name[0] != '\0')
        line += " by " + std::string(this->facts_.record_name);
      if (this->facts_.record_year > 0)
        line += " in " + std::to_string(this->facts_.record_year);
    }
    add_part(t, line);
  }
  return t;
}

void F1App::frame(int w, int h, int64_t now, AppFrame &out) const {
  F1Screen list[8];
  if (this->rotation(now, list, 8) == 0) {
    this->redraw_ = 60000;
    out.message = "F1";
    const bool tried = this->last_[(int) Read::HEADER] != 0 || this->last_[(int) Read::NEXT] != 0;
    if (this->host_[HOST_ESPN].fails >= kF1FailOver && this->jolpica_out_())
      out.ticker = "Can't reach the F1 schedule";
    else if (this->ok_[(int) Read::NEXT] != 0 && !this->sched_.valid)
      out.ticker = "No F1 races scheduled";
    else
      out.ticker = tried ? "Reading the F1 schedule" : "F1";
    return;
  }
  uint32_t left = 0;
  const F1Screen s = this->screen(now, &left);
  // Each draw reaches Home Assistant: only when it changes.
  uint32_t next = left > 0 ? left * 1000 : 60000;
  if (s == F1Screen::NEXT) {
    const F1Schedule &sc = this->schedule(now);
    for (size_t i = 0; i < sc.n; i++)
      if (sc.sessions[i].start > now) {
        const int64_t until = sc.sessions[i].start - now;
        const uint32_t tick = until <= kHour ? 1000 : (uint32_t) (60 - now % 60) * 1000;
        next = next < tick ? next : tick;
        break;
      }
  }
  this->redraw_ = next;
  switch (s) {
    case F1Screen::LIVE:
      this->live_frame_(w, now, out);
      break;
    case F1Screen::NEXT:
      this->next_frame_(w, now, out);
      break;
    case F1Screen::CIRCUIT:
      this->circuit_frame_(w, h, now, out);
      break;
    case F1Screen::WEATHER:
      this->weather_frame_(w, now, out);
      break;
    case F1Screen::DRIVERS:
      this->standings_frame_(this->drivers_, w, now, out);
      break;
    case F1Screen::CONSTRUCTORS:
      this->standings_frame_(this->teams_, w, now, out);
      break;
    case F1Screen::LAST_RACE:
      this->results_frame_(w, out);
      break;
  }
  // On track the order's ticker, else the rotation's.
  if (s != F1Screen::LIVE || !racing_on_track(this->live_.snap, now)) {
    out.spans.clear();
    out.ticker = this->ticker_(now, out.spans);
  }
}

}
