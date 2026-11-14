// The Weather display (app_weather.h).
#include "app_weather.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "board_layout.h"
#include "app_flights.h"
#include "flights.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace wx {

using espn::AppAlign;
using espn::AppFrame;
using espn::AppText;
using esphome::gameday::BoardFont;
using esphome::gameday::font_px;
using esphome::gameday::TickerSpan;

namespace {


const uint32_t kWhite = 0xFFFFFF, kGray = 0xA0A0A0, kDim = 0x606060, kSky = 0xD0D0D0;
const uint32_t kWarm = 0xFFB060, kCool = 0x80B4FF, kWet = 0x50A0FF, kRing = 0x484848;
const int64_t kDaySecs = 86400;

size_t glyphs(const std::string &s) {
  size_t n = 0;
  for (char c : s)
    if (((unsigned char) c & 0xC0) != 0x80)
      n++;
  return n;
}

int px_of(const std::string &s, BoardFont f) { return (int) glyphs(s) * font_px(f); }

void put(AppFrame &f, int x, int y, int w, BoardFont font, AppAlign al, uint32_t color, const std::string &s) {
  if (s.empty() || w < font_px(font))
    return;
  AppText t;
  t.x = (int16_t) x;
  t.y = (int16_t) y;
  t.w = (int16_t) w;
  t.font = font;
  t.align = al;
  t.color = color;
  t.text = espn::f1_fit(s, (size_t) (w / font_px(font)));
  f.texts.push_back(std::move(t));
}

__attribute__((format(printf, 8, 9))) void putf(AppFrame &f, int x, int y, int w, BoardFont font, AppAlign al,
                                                uint32_t color, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  put(f, x, y, w, font, al, color, buf);
}

// Labels and values: two a row, or stacked in columns.
struct Stats {
  AppFrame &f;
  bool beside;
  int max;
  int n{0};
  __attribute__((format(printf, 4, 5))) void add(const char *label, uint32_t color, const char *fmt, ...) {
    if (this->n >= this->max)
      return;
    char buf[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    const int i = this->n++;
    if (this->beside) {
      const int x = (i % 2) * 67, y = 74 + (i / 2) * 10;
      put(this->f, x, y, 25, BoardFont::S8, AppAlign::LEFT, kGray, label);
      put(this->f, x + 26, y, 35, BoardFont::S8, AppAlign::RIGHT, color, buf);
    } else {
      const int x = 132 + (i / 3) * 64, y = 1 + (i % 3) * 18;
      put(this->f, x, y, 60, BoardFont::S8, AppAlign::LEFT, kGray, label);
      put(this->f, x, y + 8, 60, BoardFont::S8, AppAlign::LEFT, color, buf);
    }
  }
};

void art(AppFrame &f, Icon i, bool big, int x, int y, int scale = 1) {
  const espn::AppArt a = icon_at(i, big, x, y, scale);
  if (a.bits != nullptr)
    f.art.push_back(a);
}

// Blue when cold, orange when hot.
uint32_t temp_color(int16_t c10) {
  static const int16_t kTo[] = {-100, 0, 100, 210, 270, 320};
  static const uint32_t kColor[] = {0x8CA8FF, 0xB4D2FF, 0xE6F2FF, 0xFFFFFF, 0xFFE0A0, 0xFFB060, 0xFF7848};
  int i = 0;
  while (i < 6 && c10 > kTo[i])
    i++;
  return kColor[i];
}

std::string pct(int p) {
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", p);
  return buf;
}

// Big digits where they fit, else smaller.
BoardFont big_font(const std::string &s, int w, BoardFont want) {
  for (BoardFont f : {want, BoardFont::S24, BoardFont::S16})
    if ((int) f <= (int) want && px_of(s, f) <= w)
      return f;
  return BoardFont::S12;
}

// The place's first part: "Brooklyn".
std::string town(const std::string &place) { return place.substr(0, place.find(',')); }

}  // namespace

std::vector<std::string> wrap(const std::string &text, size_t max) {
  std::vector<std::string> lines;
  std::string line;
  size_t at = 0;
  if (max == 0)
    return lines;
  while (at < text.size()) {
    size_t sp = text.find(' ', at);
    if (sp == std::string::npos)
      sp = text.size();
    std::string word = text.substr(at, sp - at);
    at = sp + 1;
    if (word.empty())
      continue;
    while (glyphs(word) > max) {
      if (!line.empty()) {
        lines.push_back(line);
        line.clear();
      }
      const std::string head = espn::f1_fit(word, max);
      lines.push_back(head);
      word = word.substr(head.size());
    }
    if (!line.empty() && glyphs(line) + 1 + glyphs(word) > max) {
      lines.push_back(line);
      line.clear();
    }
    line += (line.empty() ? "" : " ") + word;
  }
  if (!line.empty())
    lines.push_back(line);
  return lines;
}

// ---- Settings and state --------------------------------------------------------------

const char *const *WeatherApp::hosts() const {
  static const char *const kList[] = {"api.open-meteo.com",  "api.met.no",         "air-quality-api.open-meteo.com",
                                      "api.weather.gov",     "api.rainviewer.com", nullptr};
  return kList;
}

espn::AppSet WeatherApp::set(const std::string &key, const std::string &value) {
  if (key != "wxticker" || (value != "0" && value != "1"))
    return espn::AppSet::REFUSED;
  const uint8_t v = value == "1" ? 1 : 0;
  if (v == this->prefs_.ticker)
    return espn::AppSet::SAME;
  this->prefs_.ticker = v;
  return espn::AppSet::CHANGED;
}

bool WeatherApp::home_(int32_t &lat, int32_t &lon) const { return espn::app_home(espn::app_slots(), lat, lon); }

bool WeatherApp::for_home_(Read r, int32_t lat, int32_t lon) const {
  return r == Read::RADAR || (this->lat_[(int) r] == lat && this->lon_[(int) r] == lon);
}

bool WeatherApp::fresh_(Read r, int64_t now) const {
  int32_t lat, lon;
  const int i = (int) r;
  if (!this->home_(lat, lon) || this->ok_[i] == 0 || !this->for_home_(r, lat, lon))
    return false;
  const int64_t age = now - this->ok_[i];
  switch (r) {
    case Read::FORECAST:
      return this->fc_.valid && age < kOldS;
    case Read::AIR:
      return this->air_.valid && age < kAirOldS;
    case Read::ALERTS:
      return this->al_.valid && age < kAlertsOldS;
    default:
      return this->rad_.valid && age < kRadarOldS;
  }
}

bool WeatherApp::nws_(int32_t lat, int32_t lon) const {
  return in_us(lat, lon) && !(lat == this->no_nws_lat_ && lon == this->no_nws_lon_);
}

bool WeatherApp::rv_(int64_t now) const { return now >= this->img_out_[0] && this->fresh_(Read::RADAR, now); }

bool WeatherApp::iem_(int64_t now) const {
  int32_t lat, lon;
  if (this->rv_(now) || now < this->img_out_[1] || !this->home_(lat, lon) || !in_us(lat, lon))
    return false;
  const espn::F1Host &rv = this->host_[HOST_RAINVIEWER];
  return rv.fails > 0 || rv.backup() || now < this->img_out_[0];
}

// One writer per value type, not one per call
__attribute__((noinline)) static void put_s(JsonObject o, const char *k, const char *v) { o[k] = v; }
__attribute__((noinline)) static void put_i(JsonObject o, const char *k, int v) { o[k] = v; }

void WeatherApp::state(JsonObject out, int64_t now) const {
  int32_t lat = 0, lon = 0;
  std::string place;
  const bool home = espn::app_home(espn::app_slots(), lat, lon, &place);
  const Units u = units_for_zone(espn::app_env().tz);
  put_i(out, "ticker", this->prefs_.ticker != 0);
  put_s(out, "units", u.f ? "imperial" : "metric");
  if (!home) {
    put_s(out, "status", "no_home");
    return;
  }
  put_s(out, "home", ::flights::coord_text(lat, lon).c_str());
  put_s(out, "place", place.c_str());
  const int64_t read = this->ok_[(int) Read::FORECAST];
  const bool mine = read != 0 && this->for_home_(Read::FORECAST, lat, lon);
  const bool failing = this->fails_ >= kNoDataFails;
  // Not read lately: off screen, or failing
  put_s(out, "status", !mine                                      ? (failing ? "failing" : "waiting")
                       : !this->on_ && now - read > 2 * kForecastS ? "old"
                       : failing                                   ? "failing"
                       : !this->fresh_(Read::FORECAST, now)        ? "waiting"
                                                                   : "ok");
  if (!mine)
    return;
  const Now &n = this->fc_.now;
  put_s(out, "source", this->fc_.src == Src::MET ? "met-norway" : "open-meteo");
  put_s(out, "temp", deg(n.temp_c10, u, true).c_str());
  put_s(out, "sky", sky_text(n.code, n.day, 20).c_str());
  if (this->fresh_(Read::AIR, now))
    put_i(out, "aqi", this->air_.aqi);
  if (this->nws_(lat, lon)) {
    const Alert *a[kAlerts];
    const size_t k = this->live_alerts_(now, a, kAlerts);
    put_i(out, "alerts", (int) k);
    if (k > 0)
      put_s(out, "alert", a[0]->event);
  }
  std::string urls[1];
  if (this->radar_urls(now, 64, 56, urls, 1) > 0)
    put_s(out, "radar", this->rv_(now) ? "rainviewer" : "iem");
}

bool WeatherApp::lit(int64_t now) const {
  const Alert *a[kAlerts];
  return this->live_alerts_(now, a, kAlerts) > 0 && a[0]->level >= Level::WATCH;
}

void WeatherApp::shown(bool on) {
  this->on_ = on;
  if (on)
    this->shown_s_ = 0;
}

// ---- Reads ---------------------------------------------------------------------------

int64_t WeatherApp::due_(Read r, int64_t now, bool on) const {
  static const int64_t kNever = INT64_MAX;
  int32_t lat, lon;
  if (!this->home_(lat, lon))
    return kNever;
  const int i = (int) r;
  auto later = [](int64_t a, int64_t b) { return a > b ? a : b; };
  // Another home or never read: now; else after success.
  auto after = [&](int64_t s) {
    return this->ok_[i] == 0 || !this->for_home_(r, lat, lon) ? 0 : this->ok_[i] + s;
  };
  switch (r) {
    case Read::FORECAST: {
      if (!on)
        return kNever;
      const espn::F1Host &om = this->host_[HOST_OPEN_METEO];
      const espn::F1Host &h = om.backup() && now < om.out_until ? this->host_[HOST_MET] : om;
      return later(after(kForecastS), h.wait_until);
    }
    case Read::ALERTS:
      if (!this->nws_(lat, lon) || (!on && this->prefs_.ticker == 0))
        return kNever;
      return later(after(kAlertsS), this->host_[HOST_NWS].wait_until);
    case Read::RADAR: {
      const espn::F1Host &rv = this->host_[HOST_RAINVIEWER];
      if (!on || (rv.backup() && now < rv.out_until))
        return kNever;
      return later(after(kRadarS), rv.wait_until);
    }
    case Read::AIR:
      if (!on)
        return kNever;
      return later(after(kAirS), this->host_[HOST_AIR].wait_until);
    default:
      return kNever;
  }
}

WeatherApp::Read WeatherApp::next_read(int64_t now, bool on, int64_t &at) const {
  static const Read kOrder[] = {Read::FORECAST, Read::ALERTS, Read::RADAR, Read::AIR};
  Read best = Read::NONE;
  at = INT64_MAX;
  for (Read r : kOrder) {
    const int64_t d = this->due_(r, now, on);
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

uint32_t WeatherApp::poll_ms(int64_t now) const {
  int64_t at;
  if (this->next_read(now, true, at) == Read::NONE)
    return 60 * 1000;
  if (at <= now || at <= this->last_any_)
    return 0;
  const int64_t s = at - this->last_any_;
  return s > 6 * 3600 ? 6 * 3600 * 1000 : (uint32_t) (s * 1000);
}

uint32_t WeatherApp::off_poll_ms_(int64_t now) const {
  int64_t at;
  if (this->next_read(now, false, at) == Read::NONE)
    return 0;
  // 0 here never reads: 1 ms is now
  if (at <= now || at <= this->last_any_)
    return 1;
  const int64_t s = at - this->last_any_;
  return s > 6 * 3600 ? 6 * 3600 * 1000 : (uint32_t) (s * 1000);
}

bool WeatherApp::request(int64_t now, espn::AppRequest &out) {
  // A read given up on counts as failed.
  if (this->inflight_ != Read::NONE) {
    this->result_ = espn::AppResult{};
    this->fail_(this->inflight_host_, now);
    this->inflight_ = Read::NONE;
  }
  int64_t at;
  const Read r = this->next_read(now, this->on_, at);
  if (r == Read::NONE || at > now || !this->home_(this->req_lat_, this->req_lon_))
    return false;
  out.body.clear();
  out.headers.clear();
  const std::string agent = ::flights::photo_agent(espn::app_env().version);
  Host host = HOST_OPEN_METEO;
  switch (r) {
    case Read::FORECAST: {
      const espn::F1Host &om = this->host_[HOST_OPEN_METEO];
      if (om.backup() && now < om.out_until) {
        out.url = met_url(this->req_lat_, this->req_lon_);
        espn::app_header(out, "User-Agent", agent);
        host = HOST_MET;
      } else {
        out.url = forecast_url(this->req_lat_, this->req_lon_);
      }
      break;
    }
    case Read::ALERTS:
      out.url = alerts_url(this->req_lat_, this->req_lon_);
      espn::app_header(out, "User-Agent", agent);
      espn::app_header(out, "Accept", "application/geo+json");
      host = HOST_NWS;
      break;
    case Read::RADAR:
      out.url = kRadarIndexUrl;
      host = HOST_RAINVIEWER;
      break;
    default:
      out.url = air_url(this->req_lat_, this->req_lon_);
      host = HOST_AIR;
      break;
  }
  this->inflight_ = r;
  this->inflight_host_ = host;
  this->result_ = espn::AppResult{};
  this->last_[(int) r] = now;
  this->last_any_ = now;
  return true;
}

bool WeatherApp::filter(JsonDocument &f) {
  switch (this->inflight_) {
    case Read::FORECAST:
      if (this->inflight_host_ == HOST_MET)
        detail::fill_met_filter(f);
      else
        detail::fill_forecast_filter(f);
      return true;
    case Read::ALERTS:
      detail::fill_alerts_filter(f);
      return true;
    case Read::RADAR:
      detail::fill_radar_filter(f);
      return true;
    case Read::AIR:
      detail::fill_air_filter(f);
      return true;
    default:
      return false;
  }
}

bool WeatherApp::parse_doc(JsonObjectConst root, int64_t now) {
  switch (this->inflight_) {
    case Read::FORECAST:
      this->p_fc_ = Forecast{};
      return this->inflight_host_ == HOST_MET ? detail::met_from_doc(root, now, this->p_fc_)
                                              : detail::forecast_from_doc(root, this->p_fc_);
    case Read::ALERTS:
      this->p_al_ = Alerts{};
      return detail::alerts_from_doc(root, now, this->p_al_);
    case Read::RADAR:
      this->p_rad_ = Radar{};
      return detail::radar_from_doc(root, this->p_rad_);
    case Read::AIR:
      this->p_air_ = Air{};
      return detail::air_from_doc(root, this->p_air_);
    default:
      return false;
  }
}

void WeatherApp::dropped() {
  this->inflight_ = Read::NONE;
  this->result_ = espn::AppResult{};
}

void WeatherApp::photo_failed(int64_t now) {
  if (this->last_page_ == Page::RADAR)
    this->img_out_[this->rv_(now) ? 0 : 1] = now + kImagesOutS;
}

void WeatherApp::fail_(Host h, int64_t now) {
  this->rnd_ = this->rnd_ * 1664525u + 1013904223u;
  const bool failover = h == HOST_OPEN_METEO || h == HOST_RAINVIEWER;
  this->host_[h].fail(now, this->result_.refused, this->result_.retry_after_ms, this->rnd_ ^ (uint32_t) now,
                      failover);
}

void WeatherApp::apply(bool ok, int64_t now) {
  const Read r = this->inflight_;
  const Host h = this->inflight_host_;
  this->inflight_ = Read::NONE;
  if (r == Read::NONE)
    return;
  int32_t lat, lon;
  // Read for a home since changed: dropped
  if (!this->home_(lat, lon) || lat != this->req_lat_ || lon != this->req_lon_) {
    this->result_ = espn::AppResult{};
    return;
  }
  if (!ok) {
    // The NWS's answer for a spot outside its area
    if (r == Read::ALERTS && this->result_.status == 400) {
      this->no_nws_lat_ = lat;
      this->no_nws_lon_ = lon;
      this->host_[h].ok();
    } else {
      this->fail_(h, now);
    }
    if (r == Read::FORECAST && this->fails_ < 255)
      this->fails_++;
    this->result_ = espn::AppResult{};
    return;
  }
  this->result_ = espn::AppResult{};
  this->host_[h].ok();
  const int i = (int) r;
  this->ok_[i] = now;
  this->lat_[i] = lat;
  this->lon_[i] = lon;
  switch (r) {
    case Read::FORECAST:
      this->fc_ = this->p_fc_;
      this->fails_ = 0;
      break;
    case Read::ALERTS:
      this->al_ = this->p_al_;
      break;
    case Read::RADAR:
      this->rad_ = this->p_rad_;
      break;
    default:
      this->air_ = this->p_air_;
      break;
  }
}

// ---- Pages ---------------------------------------------------------------------------

size_t WeatherApp::live_alerts_(int64_t now, const Alert **out, size_t cap) const {
  int32_t lat, lon;
  if (!this->home_(lat, lon) || !this->al_.valid || this->ok_[(int) Read::ALERTS] == 0 ||
      !this->for_home_(Read::ALERTS, lat, lon) || !this->nws_(lat, lon))
    return 0;
  const int64_t read = this->ok_[(int) Read::ALERTS];
  size_t n = 0;
  for (size_t i = 0; i < this->al_.n && n < cap; i++) {
    const Alert &a = this->al_.a[i];
    // No end given: an hour after the last read
    if ((a.ends != 0 && a.ends <= now) || (a.ends == 0 && now - read > kAlertsOldS))
      continue;
    out[n++] = &a;
  }
  return n;
}

size_t WeatherApp::radar_urls(int64_t now, int w, int h, std::string *out, size_t cap) const {
  int32_t lat, lon;
  if (!this->home_(lat, lon))
    return 0;
  size_t n = 0;
  if (this->rv_(now)) {
    for (size_t i = 0; i < this->rad_.n && n < cap; i++)
      out[n++] = rainviewer_url(this->rad_, i, lat, lon);
  } else if (this->iem_(now)) {
    for (size_t i = 0; i < kFrames && n < cap; i++)
      out[n++] = iem_url(iem_frame_time(now, i, kFrames), lat, lon, w, h);
  }
  return n;
}

size_t WeatherApp::rotation(int64_t now, Page *out, size_t cap) const {
  size_t n = 0;
  auto add = [&](Page p) {
    if (n < cap)
      out[n++] = p;
  };
  if (!this->fresh_(Read::FORECAST, now))
    return 0;
  const Alert *a[kAlerts];
  if (this->live_alerts_(now, a, kAlerts) > 0)
    add(Page::ALERTS);
  add(Page::NOW);
  if (this->fc_.n_hours >= 4)
    add(Page::HOURLY);
  if (this->fc_.n_days >= 3)
    add(Page::DAILY);
  std::string u[1];
  if (this->radar_urls(now, 64, 56, u, 1) > 0)
    add(Page::RADAR);
  if (this->fresh_(Read::AIR, now))
    add(Page::AIR);
  return n;
}

static uint32_t page_s(Page p) { return p == Page::ALERTS || p == Page::RADAR ? 12 : p == Page::AIR ? 8 : 10; }

Page WeatherApp::page(int64_t now, uint32_t *left) const {
  Page list[kPages];
  const size_t n = this->rotation(now, list, kPages);
  if (left != nullptr)
    *left = 0;
  if (n <= 1)
    return n == 1 ? list[0] : Page::NOW;
  uint32_t total = 0;
  for (size_t i = 0; i < n; i++)
    total += page_s(list[i]);
  const int64_t start = this->shown_s_ != 0 && this->shown_s_ <= now ? this->shown_s_ : now;
  uint32_t t = (uint32_t) ((uint64_t) (now - start) % total);
  for (size_t i = 0; i < n; i++) {
    if (t < page_s(list[i])) {
      if (left != nullptr)
        *left = page_s(list[i]) - t;
      return list[i];
    }
    t -= page_s(list[i]);
  }
  return list[0];
}

// "Winter Storm Warning until 10a Sat"
static std::string alert_head(const Alert &a, int64_t now) {
  std::string s = a.event;
  if (a.ends != 0) {
    const struct tm e = espn::app_local(a.ends), n = espn::app_local(now);
    char buf[24];
    snprintf(buf, sizeof buf, " until %s%s%s", (e.tm_min == 0 ? hour_text(e) : clock_text(e)).c_str(),
             e.tm_yday != n.tm_yday || e.tm_year != n.tm_year ? " " : "",
             e.tm_yday != n.tm_yday || e.tm_year != n.tm_year ? day_name(e) : "");
    s += buf;
  }
  return s;
}

std::string WeatherApp::ticker_warning(int64_t now, uint32_t &color) const {
  if (this->prefs_.ticker == 0)
    return "";
  const Alert *a[kAlerts];
  const size_t n = this->live_alerts_(now, a, kAlerts);
  std::string out;
  for (size_t i = 0; i < n; i++) {
    if (a[i]->level < Level::WATCH)
      break;
    if (out.empty())
      color = level_color(a[i]->level);
    out += (out.empty() ? "" : " | ") + alert_head(*a[i], now);
  }
  return out;
}

// Appends a printf'd part, " | " between parts.
__attribute__((format(printf, 2, 3))) static void addf(std::string &out, const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (!out.empty())
    out += " | ";
  out += buf;
}

std::string WeatherApp::ticker_(int64_t now, std::vector<TickerSpan> &spans) const {
  const Units u = units_for_zone(espn::app_env().tz);
  const char *deg_u = u.f ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
  std::string out;
  // Every alert's head first, then its words
  const Alert *a[kAlerts];
  const size_t na = this->live_alerts_(now, a, kAlerts);
  for (size_t i = 0; i < na; i++) {
    const std::string head = alert_head(*a[i], now);
    espn::app_span(spans, head, level_color(a[i]->level));
    addf(out, "%s", head.c_str());
  }
  for (size_t i = 0; i < na; i++)
    if (a[i]->text[0] != '\0')
      out.append(" | ").append(a[i]->event).append(": ").append(a[i]->text);
  const Now &n = this->fc_.now;
  int32_t lat, lon;
  std::string place;
  espn::app_home(espn::app_slots(), lat, lon, &place);
  place = town(place);
  addf(out, "%s%s%d%s, %s", place.c_str(), place.empty() ? "" : ": ", temp_of(n.temp_c10, u.f), deg_u,
       sky_text(n.code, n.day, 20).c_str());
  if (n.feels_c10 != n.temp_c10)
    addf(out, "Feels like %d", temp_of(n.feels_c10, u.f));
  if (this->fc_.n_days > 0) {
    const Day &d = this->fc_.days[0];
    addf(out, "Today %d/%d", temp_of(d.hi_c10, u.f), temp_of(d.lo_c10, u.f));
    if (d.pop > 0)
      addf(out, "%d%% chance of rain", d.pop);
    if (d.sunrise != 0)
      addf(out, "Sunrise %s, sunset %s", clock_text(espn::app_local(d.sunrise)).c_str(),
           clock_text(espn::app_local(d.sunset)).c_str());
  }
  addf(out, "Wind %s", wind_text(n.wind_kmh10, n.wind_dir, u, false).c_str());
  if (n.gust_kmh10 > n.wind_kmh10 + 50)
    addf(out, "Gusts %d", speed_of(n.gust_kmh10, u.mph));
  if (n.humidity >= 0)
    addf(out, "Humidity %d%%", n.humidity);
  if (this->fresh_(Read::AIR, now)) {
    const int b = aqi_band(this->air_.aqi);
    const size_t at = out.size() + 3;
    addf(out, "Air quality %d %s", this->air_.aqi, aqi_name(b, false));
    espn::app_span(spans, out.substr(at), aqi_color(b));
  }
  return out;
}

void WeatherApp::note_(int w, int h, const char *text, AppFrame &out) const {
  const std::vector<std::string> lines = wrap(text, (size_t) w / 5);
  int y = (h - 8 - (int) lines.size() * 8) / 2;
  for (const std::string &l : lines) {
    put(out, 0, y, w, BoardFont::S8, AppAlign::CENTER, kWhite, l);
    y += 8;
  }
}

void WeatherApp::frame(int w, int h, int64_t now, AppFrame &out) const {
  int32_t lat, lon;
  this->redraw_ = 1000;
  if (!this->home_(lat, lon)) {
    this->note_(w, h, "Set your home on the device page", out);
    return;
  }
  if (!this->fresh_(Read::FORECAST, now)) {
    this->note_(w, h, this->fails_ >= kNoDataFails ? "Can't reach weather data" : "Loading weather", out);
    return;
  }
  if (this->shown_s_ == 0 || now < this->shown_s_)
    this->shown_s_ = now;
  uint32_t left = 0;
  const Page p = this->page(now, &left);
  if (p == Page::RADAR && this->last_page_ != Page::RADAR)
    this->radar_ms_ = espn::app_ms();
  this->last_page_ = p;
  this->redraw_ = left > 0 ? left * 1000 : 1000;
  out.ticker = this->ticker_(now, out.spans);
  switch (p) {
    case Page::ALERTS:
      this->alerts_(w, h, now, out);
      break;
    case Page::HOURLY:
      this->hourly_(w, h, now, out);
      break;
    case Page::DAILY:
      this->daily_(w, h, now, out);
      break;
    case Page::RADAR:
      this->radar_(w, h, now, out);
      break;
    case Page::AIR:
      this->aqi_(w, h, out);
      break;
    default:
      this->now_(w, h, now, out);
      break;
  }
}

void WeatherApp::now_(int w, int h, int64_t now, AppFrame &out) const {
  const Units u = units_for_zone(espn::app_env().tz);
  const Now &n = this->fc_.now;
  const Day *today = this->fc_.n_days > 0 ? &this->fc_.days[0] : nullptr;
  const std::string t = deg(n.temp_c10, u);
  const uint32_t tc = temp_color(n.temp_c10);
  const int feels = temp_of(n.feels_c10, u.f);
  // The next sunrise and sunset, today's or tomorrow's
  int64_t rise = 0, set = 0;
  for (size_t i = 0; i < this->fc_.n_days && i < 2; i++) {
    const Day &d = this->fc_.days[i];
    if (rise == 0 && d.sunrise > now)
      rise = d.sunrise;
    if (set == 0 && d.sunset > now)
      set = d.sunset;
  }
  const bool air = this->fresh_(Read::AIR, now);
  const int band = aqi_band(this->air_.aqi);
  if (h >= 128) {
    art(out, now_icon(n), true, 0, 0, 3);
    put(out, 50, 2, 78, big_font(t, 78, BoardFont::S32), AppAlign::CENTER, tc, t);
    if (today != nullptr) {
      putf(out, 50, 34, 39, BoardFont::S8, AppAlign::CENTER, kWarm, "H %d", temp_of(today->hi_c10, u.f));
      putf(out, 89, 34, 39, BoardFont::S8, AppAlign::CENTER, kCool, "L %d", temp_of(today->lo_c10, u.f));
    }
    put(out, 0, 52, 128, BoardFont::S16, AppAlign::CENTER, kSky, sky_text(n.code, n.day, 16));
    Stats st{out, true, 8};
    st.add("Feels", temp_color(n.feels_c10), "%d", feels);
    if (n.humidity >= 0)
      st.add("Humid", kWet, "%d%%", n.humidity);
    st.add("Wind", kWhite, "%s", wind_text(n.wind_kmh10, n.wind_dir, u, true).c_str());
    st.add("Gusts", kWhite, "%d%s", speed_of(n.gust_kmh10, u.mph), u.mph ? "mph" : "km/h");
    if (rise != 0)
      st.add("Rise", kWarm, "%s", clock_text(espn::app_local(rise)).c_str());
    if (set != 0)
      st.add("Set", kWarm, "%s", clock_text(espn::app_local(set)).c_str());
    if (today != nullptr && today->pop >= 0)
      st.add("Rain", kWet, "%d%%", today->pop);
    if (air)
      st.add("Air", aqi_color(band), "%d", this->air_.aqi);
    return;
  }
  const std::string hum = n.humidity >= 0 ? pct(n.humidity) : std::string();
  if (w < 128) {
    art(out, now_icon(n), true, 0, 2);
    const BoardFont f = big_font(t, 48, BoardFont::S24);
    put(out, 16, f == BoardFont::S24 ? 0 : 4, 48, f, AppAlign::RIGHT, tc, t);
    put(out, 0, 21, 64, BoardFont::S8, AppAlign::LEFT, kSky, sky_text(n.code, n.day, 12));
    if (today != nullptr) {
      putf(out, 0, 29, 32, BoardFont::S8, AppAlign::LEFT, kWarm, "H%d", temp_of(today->hi_c10, u.f));
      putf(out, 32, 29, 32, BoardFont::S8, AppAlign::RIGHT, kCool, "L%d", temp_of(today->lo_c10, u.f));
    }
    const int hw = px_of(hum, BoardFont::S8);
    put(out, 0, 37, 64 - hw - 3, BoardFont::S8, AppAlign::LEFT, kWhite, wind_text(n.wind_kmh10, n.wind_dir, u, true));
    put(out, 64 - hw, 37, hw, BoardFont::S8, AppAlign::RIGHT, kWet, hum);
    putf(out, 0, 45, 64, BoardFont::S8, AppAlign::LEFT, kGray, "Feels %d", feels);
    return;
  }
  // The 128 card, then extras on wide boards
  art(out, now_icon(n), true, 0, 0, 2);
  const BoardFont f = big_font(t, 62, BoardFont::S32);
  put(out, 33, f == BoardFont::S32 ? 0 : 4, 62, f, AppAlign::CENTER, tc, t);
  if (today != nullptr) {
    putf(out, 96, 3, 32, BoardFont::S8, AppAlign::RIGHT, kWarm, "H%d", temp_of(today->hi_c10, u.f));
    putf(out, 96, 13, 32, BoardFont::S8, AppAlign::RIGHT, kCool, "L%d", temp_of(today->lo_c10, u.f));
  }
  put(out, 96, 23, 32, BoardFont::S8, AppAlign::RIGHT, kWet, hum);
  put(out, 0, 33, 128, BoardFont::S12, AppAlign::LEFT, kSky, sky_text(n.code, n.day, 21));
  const std::string wind = wind_text(n.wind_kmh10, n.wind_dir, u, false);
  const int ww = px_of(wind, BoardFont::S8);
  putf(out, 0, 47, 127 - ww - 4, BoardFont::S8, AppAlign::LEFT, kGray, "Feels %d", feels);
  put(out, 127 - ww, 47, ww + 1, BoardFont::S8, AppAlign::RIGHT, kWhite, wind);
  if (w < 192)
    return;
  Stats col{out, false, (w - 128) / 64 * 3};
  if (rise != 0)
    col.add("Sunrise", kWarm, "%s", clock_text(espn::app_local(rise)).c_str());
  if (set != 0)
    col.add("Sunset", kWarm, "%s", clock_text(espn::app_local(set)).c_str());
  if (air)
    col.add("Air quality", aqi_color(band), "%d %s", this->air_.aqi, aqi_name(band, true));
  if (today != nullptr && today->pop >= 0)
    col.add("Rain today", kWet, "%d%%", today->pop);
  if (n.gust_kmh10 > 0)
    col.add("Gusts", kWhite, "%d %s", speed_of(n.gust_kmh10, u.mph), u.mph ? "mph" : "km/h");
}

void WeatherApp::hourly_(int w, int h, int64_t now, AppFrame &out) const {
  const Units u = units_for_zone(espn::app_env().tz);
  const Forecast &fc = this->fc_;
  // The first hour not yet over
  size_t first = 0;
  while (first + 1 < fc.n_hours && fc.hours[first + 1].at <= now)
    first++;
  auto when = [&](size_t i) { return i == first ? std::string("Now") : hour_text(espn::app_local(fc.hours[i].at)); };
  if (w < 128 && h < 128) {
    // A row every three hours
    for (int r = 0; r < 5; r++) {
      const size_t i = first + (size_t) r * 3;
      if (i >= fc.n_hours)
        break;
      const Hour &hr = fc.hours[i];
      const int y = r * 11;
      put(out, 0, y + 1, 17, BoardFont::S8, AppAlign::LEFT, kGray, when(i));
      art(out, icon_for(hr.code, hr.day), false, 18, y);
      putf(out, 28, y + 1, 15, BoardFont::S8, AppAlign::RIGHT, temp_color(hr.temp_c10), "%d", temp_of(hr.temp_c10, u.f));
      if (hr.pop >= 10)
        put(out, 44, y + 1, 20, BoardFont::S8, AppAlign::RIGHT, kWet, pct(hr.pop));
    }
    return;
  }
  const bool tall = h >= 128;
  const int cols = tall ? 4 : w < 192 ? 6 : 8;
  const int step = tall || w >= 256 ? 3 : 2;
  const int cw = w / cols, x0 = (w - cw * cols) / 2;
  for (int r = 0; r < (tall ? 2 : 1); r++) {
    const int y0 = r * 60;
    for (int c = 0; c < cols; c++) {
      const size_t i = first + (size_t) ((r * cols + c) * step);
      if (i >= fc.n_hours)
        break;
      const Hour &hr = fc.hours[i];
      const int x = x0 + c * cw;
      put(out, x, y0, cw, BoardFont::S8, AppAlign::CENTER, kGray, when(i));
      if (tall) {
        art(out, icon_for(hr.code, hr.day), true, x + (cw - 32) / 2, y0 + 8, 2);
        put(out, x, y0 + 40, cw, BoardFont::S12, AppAlign::CENTER, temp_color(hr.temp_c10), deg(hr.temp_c10, u));
        if (hr.pop >= 10)
          put(out, x, y0 + 51, cw, BoardFont::S8, AppAlign::CENTER, kWet, pct(hr.pop));
        continue;
      }
      art(out, icon_for(hr.code, hr.day), true, x + (cw - 16) / 2, 9);
      putf(out, x, 26, cw, BoardFont::S8, AppAlign::CENTER, temp_color(hr.temp_c10), "%d", temp_of(hr.temp_c10, u.f));
      if (hr.pop >= 10)
        put(out, x, 34, cw, BoardFont::S8, AppAlign::CENTER, kWet, pct(hr.pop));
    }
  }
  if (tall)
    return;
  // The temperature's line under the columns, an hour a step
  const int top = 44, bottom = 53;
  int16_t lo = INT16_MAX, hi = INT16_MIN;
  const size_t span = (size_t) (cols * step);
  for (size_t i = first; i < fc.n_hours && i < first + span; i++) {
    lo = std::min(lo, fc.hours[i].temp_c10);
    hi = std::max(hi, fc.hours[i].temp_c10);
  }
  for (size_t i = first; i < fc.n_hours && i < first + span; i++) {
    const int x = x0 + cw / 2 + (int) (i - first) * cw / step;
    if (x >= w)
      break;
    const int y = hi > lo ? bottom - (fc.hours[i].temp_c10 - lo) * (bottom - top) / (hi - lo) : (top + bottom) / 2;
    out.line.pts.push_back({(int16_t) x, (int16_t) y});
  }
  out.line.color = 0xC07838;
}

void WeatherApp::daily_(int w, int h, int64_t now, AppFrame &out) const {
  (void) now;
  const Units u = units_for_zone(espn::app_env().tz);
  const Forecast &fc = this->fc_;
  auto day = [&](const Day &d) { return day_name(espn::app_local(d.at + kDaySecs / 2)); };
  if (h >= 128 || w < 128) {
    // A row a day
    const bool tall = h >= 128;
    for (int i = 0; i < (tall ? 7 : 5) && i < fc.n_days; i++) {
      const Day &d = fc.days[i];
      if (tall) {
        const int y = i * 16;
        put(out, 0, y + 2, 24, BoardFont::S12, AppAlign::LEFT, kGray, day(d));
        art(out, icon_for(d.code, true), true, 26, y);
        put(out, 44, y + 2, 30, BoardFont::S12, AppAlign::RIGHT, kWarm, deg(d.hi_c10, u));
        put(out, 76, y + 2, 30, BoardFont::S12, AppAlign::RIGHT, kCool, deg(d.lo_c10, u));
        if (d.pop >= 10)
          put(out, 108, y + 4, 20, BoardFont::S8, AppAlign::RIGHT, kWet, pct(d.pop));
        continue;
      }
      const int y = i * 11;
      put(out, 0, y + 1, 15, BoardFont::S8, AppAlign::LEFT, kGray, day(d));
      art(out, icon_for(d.code, true), false, 17, y);
      putf(out, 28, y + 1, 17, BoardFont::S8, AppAlign::RIGHT, kWarm, "%d", temp_of(d.hi_c10, u.f));
      putf(out, 46, y + 1, 18, BoardFont::S8, AppAlign::RIGHT, kCool, "%d", temp_of(d.lo_c10, u.f));
    }
    return;
  }
  const int cols = w < 192 ? 6 : 7;
  const int cw = w / cols, x0 = (w - cw * cols) / 2;
  for (int c = 0; c < cols && c < fc.n_days; c++) {
    const Day &d = fc.days[c];
    const int x = x0 + c * cw;
    put(out, x, 0, cw, BoardFont::S8, AppAlign::CENTER, kGray, day(d));
    art(out, icon_for(d.code, true), true, x + (cw - 16) / 2, 9);
    putf(out, x, 26, cw, BoardFont::S8, AppAlign::CENTER, kWarm, "%d", temp_of(d.hi_c10, u.f));
    putf(out, x, 34, cw, BoardFont::S8, AppAlign::CENTER, kCool, "%d", temp_of(d.lo_c10, u.f));
    if (d.pop >= 10)
      put(out, x, 44, cw, BoardFont::S8, AppAlign::CENTER, kWet, pct(d.pop));
  }
}

void WeatherApp::radar_(int w, int h, int64_t now, AppFrame &out) const {
  int32_t lat, lon;
  this->home_(lat, lon);
  const int bw = std::min(w, 128), bh = h - 8;
  std::string urls[kFrames];
  const size_t n = this->radar_urls(now, bw, bh, urls, kFrames);
  if (n == 0)
    return;
  const uint32_t ms = espn::app_ms() - this->radar_ms_;
  const uint32_t step = ms / kStepMs % ((uint32_t) n + kHoldSteps);
  const size_t k = std::min<size_t>(step, n - 1);
  // Redrawn as the next step starts
  if (n > 1)
    this->redraw_ = std::min<uint32_t>(this->redraw_, kStepMs - ms % kStepMs);
  espn::AppPhoto &ph = out.photo;
  ph.w = (uint8_t) bw;
  ph.h = (uint8_t) bh;
  ph.url = urls[k];
  ph.hold = urls[n - 1];
  ph.keep_art = true;
  // Said until a frame draws
  ph.alt = (int8_t) out.texts.size();
  put(out, 0, bh / 2 - 4, bw, BoardFont::S8, AppAlign::CENTER, kGray, bw >= 70 ? "Loading radar" : "Loading");
  const bool rv = this->rv_(now);
  const int64_t t = rv ? this->rad_.t[k] : iem_frame_time(now, k, kFrames);
  put(out, 1, 0, 30, BoardFont::S8, AppAlign::LEFT, k + 1 == n ? kWhite : kGray, clock_text(espn::app_local(t)));
  const Units u = units_for_zone(espn::app_env().tz);
  const char *unit = u.mph ? "mi" : "km";
  int dist = 0, r = 0;
  const int cx = bw / 2, cy = bh / 2;
  if (radar_ring(lat, bw, bh, u.mph, dist, r)) {
    for (int i = 0; i <= 40; i++) {
      const float a = (float) i * 6.2831853f / 40.0f;
      out.line.pts.push_back({(int16_t) lroundf(cx + r * cosf(a)), (int16_t) lroundf(cy + r * sinf(a))});
    }
    out.line.color = kRing;
    putf(out, bw - 26, bh - 8, 25, BoardFont::S8, AppAlign::RIGHT, kDim, "%d%s", dist, unit);
  }
  out.bar = espn::AppFill{(int16_t) (cx - 1), (int16_t) (cy - 1), 2, 2, kWhite};
  if (w < 192)
    return;
  // Beside it on wide boards: now and coming rain
  const Now &nw = this->fc_.now;
  art(out, now_icon(nw), true, 132, 2);
  put(out, 150, 2, 40, BoardFont::S16, AppAlign::LEFT, temp_color(nw.temp_c10), deg(nw.temp_c10, u));
  put(out, 132, 22, 58, BoardFont::S8, AppAlign::LEFT, kSky, sky_text(nw.code, nw.day, 11));
  put(out, 132, 32, 58, BoardFont::S8, AppAlign::LEFT, kGray, rv ? "RainViewer" : "NWS radar");
  if (dist != 0)
    putf(out, 132, 42, 58, BoardFont::S8, AppAlign::LEFT, kDim, "Ring %d %s", dist, unit);
  if (w < 256)
    return;
  put(out, 196, 0, 60, BoardFont::S8, AppAlign::LEFT, kGray, "Rain");
  size_t i = 0;
  while (i + 1 < this->fc_.n_hours && this->fc_.hours[i + 1].at <= now)
    i++;
  for (int row = 0; row < 5 && i < this->fc_.n_hours; row++, i++) {
    const Hour &hr = this->fc_.hours[i];
    put(out, 196, 9 + row * 9, 20, BoardFont::S8, AppAlign::LEFT, kGray, hour_text(espn::app_local(hr.at)));
    put(out, 220, 9 + row * 9, 35, BoardFont::S8, AppAlign::RIGHT, hr.pop >= 30 ? kWet : kDim,
        pct(hr.pop < 0 ? 0 : hr.pop));
  }
}

void WeatherApp::alerts_(int w, int h, int64_t now, AppFrame &out) const {
  const Alert *a[kAlerts];
  const size_t n = this->live_alerts_(now, a, kAlerts);
  if (n == 0)
    return;
  const Alert &al = *a[0];
  const uint32_t c = level_color(al.level);
  const bool small = w < 128;
  // A warning sign at the top right, room kept
  const int room = small ? w : w - 18;
  if (!small) {
    const IconArt *ia = hazard_art();
    espn::AppArt sign;
    sign.x = (int16_t) (w - 16);
    sign.w = ia->w;
    sign.h = ia->h;
    sign.bits = ia->bits;
    sign.ink[0] = c;
    out.art.push_back(sign);
  }
  BoardFont f = small ? BoardFont::S12 : BoardFont::S16;
  std::vector<std::string> lines = wrap(al.event, (size_t) room / font_px(f));
  if (lines.size() > 3) {
    f = BoardFont::S8;
    lines = wrap(al.event, (size_t) room / 5);
  }
  const int lh = esphome::gameday::font_rows(f);
  int y = 0;
  for (size_t i = 0; i < lines.size() && i < 3; i++, y += lh)
    put(out, 0, y, room, f, AppAlign::LEFT, c, lines[i]);
  y += 2;
  if (al.ends != 0) {
    const std::string head = alert_head(al, now);
    const size_t at = head.find(" until ");
    if (at != std::string::npos) {
      putf(out, 0, y, w, BoardFont::S8, AppAlign::LEFT, kWhite, "%s%s", small ? "Ends" : "Until", head.c_str() + at + 6);
      y += 9;
    }
  }
  const int limit = h - 8 - (n > 1 ? 9 : 0);
  if (h >= 128 || w >= 192) {
    // Its words, as many lines as fit
    for (const std::string &l : wrap(al.text, (size_t) w / 5)) {
      if (y + 8 > limit)
        break;
      put(out, 0, y, w, BoardFont::S8, AppAlign::LEFT, kSky, l);
      y += 8;
    }
  }
  if (n > 1 && y + 8 <= h - 8)
    putf(out, 0, h - 16, w, BoardFont::S8, AppAlign::LEFT, kGray, "+%d more: %s", (int) n - 1, a[1]->event);
}

void WeatherApp::aqi_(int w, int h, AppFrame &out) const {
  const Air &a = this->air_;
  const int b = aqi_band(a.aqi);
  const uint32_t c = aqi_color(b);
  const std::string num = std::to_string(a.aqi);
  const std::string main = a.main != Pollutant::NONE ? std::string("Mostly ") + pollutant_name(a.main) : "";
  char pm[16] = "";
  if (a.pm25_10 >= 0)
    snprintf(pm, sizeof pm, "PM2.5 %d", (a.pm25_10 + 5) / 10);
  if (h >= 128) {
    put(out, 0, 0, w, BoardFont::S12, AppAlign::CENTER, kGray, "Air quality");
    put(out, 0, 14, w, BoardFont::S32, AppAlign::CENTER, c, num);
    int y = 48;
    for (const std::string &l : wrap(aqi_name(b, false), (size_t) w / 8)) {
      put(out, 0, y, w, BoardFont::S16, AppAlign::CENTER, c, l);
      y += 16;
    }
    put(out, 0, y + 4, w, BoardFont::S8, AppAlign::CENTER, kSky, main);
    put(out, 0, y + 14, w, BoardFont::S8, AppAlign::CENTER, kGray, pm);
    return;
  }
  if (w < 128) {
    put(out, 0, 0, w, BoardFont::S8, AppAlign::CENTER, kGray, "Air quality");
    put(out, 0, 6, w, BoardFont::S24, AppAlign::CENTER, c, num);
    put(out, 0, 28, w, BoardFont::S8, AppAlign::CENTER, c, aqi_name(b, true));
    put(out, 0, 37, w, BoardFont::S8, AppAlign::CENTER, kSky, main);
    put(out, 0, 45, w, BoardFont::S8, AppAlign::CENTER, kGray, pm);
    return;
  }
  put(out, 0, 0, 64, BoardFont::S8, AppAlign::CENTER, kGray, "Air quality");
  put(out, 0, 10, 64, BoardFont::S32, AppAlign::CENTER, c, num);
  std::vector<std::string> name = wrap(aqi_name(b, false), 10);
  const BoardFont nf = name.size() <= 2 ? BoardFont::S12 : BoardFont::S8;
  if (nf == BoardFont::S8)
    name = wrap(aqi_name(b, false), 12);
  int y = 2;
  for (const std::string &l : name) {
    put(out, 66, y, 62, nf, AppAlign::LEFT, c, l);
    y += esphome::gameday::font_rows(nf) + 1;
  }
  put(out, 66, 38, 62, BoardFont::S8, AppAlign::LEFT, kSky, main);
  put(out, 66, 47, 62, BoardFont::S8, AppAlign::LEFT, kGray, pm);
  if (w < 192)
    return;
  // The scale beside it, this band lit
  static const int kFrom[] = {0, 51, 101, 151, 201, 301};
  for (int i = 0; i < 6; i++) {
    const uint32_t col = i == b ? aqi_color(i) : kDim;
    if (w >= 256)
      putf(out, 132, 1 + i * 9, w - 132, BoardFont::S8, AppAlign::LEFT, col, "%d+ %s", kFrom[i],
           i == 2 ? "Sensitive groups" : aqi_name(i, false));
    else
      put(out, 132, 1 + i * 9, w - 132, BoardFont::S8, AppAlign::LEFT, col, aqi_name(i, true));
  }
}

}  // namespace wx
