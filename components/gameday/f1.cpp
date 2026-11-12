// F1 display text, geometry, circuits, timing and frames.
#include "f1.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "board_layout.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace espn {

using detail::add_part;
using detail::cut;
using detail::whole_words;

static const uint32_t kWhite = 0xFFFFFF;
static const uint32_t kGray = 0xA0A0A0;
static const uint32_t kDim = 0x606060;
static const uint32_t kGold = 0xFFD000;
static const uint32_t kAmber = 0xFFA000;
static const uint32_t kRed = 0xFF3030;
static const uint32_t kBlue = 0x40A0FF;
static const int64_t kHour = 3600;
static const int64_t kDay = 24 * kHour;
static const uint32_t kMinuteMs = 60 * 1000;

static void put(char *dst, size_t cap, const std::string &s) { GolfRow::put(dst, cap, s); }

std::string f1_jolpica_url(F1Read read) {
  const char *path;
  switch (read) {
    case F1Read::NEXT:
      path = "next";
      break;
    case F1Read::DRIVERS:
      path = "driverStandings";
      break;
    case F1Read::CONSTRUCTORS:
      path = "constructorStandings";
      break;
    case F1Read::LAST_RESULTS:
      path = "last/results";
      break;
    default:
      return "";
  }
  return std::string("https://api.jolpi.ca/ergast/f1/current/") + path + ".json";
}

std::string f1_round_url(uint8_t round) {
  return "https://api.jolpi.ca/ergast/f1/current/" + std::to_string(round) + ".json";
}

std::string f1_circuit_url(uint32_t espn_id) {
  return "https://sports.core.api.espn.com/v2/sports/racing/leagues/f1/circuits/" + std::to_string(espn_id);
}

std::string f1_athlete_url(uint32_t espn_id) {
  return "https://sports.core.api.espn.com/v2/sports/racing/athletes/" + std::to_string(espn_id);
}

static std::string degrees(int32_t e4) {
  char buf[16];
  uint32_t a = (uint32_t) (e4 < 0 ? -(int64_t) e4 : e4);
  snprintf(buf, sizeof(buf), "%s%u.%04u", e4 < 0 ? "-" : "", (unsigned) (a / 10000), (unsigned) (a % 10000));
  return buf;
}

std::string f1_weather_url(int32_t lat_e4, int32_t lon_e4) {
  return "https://api.open-meteo.com/v1/forecast?latitude=" + degrees(lat_e4) + "&longitude=" + degrees(lon_e4) +
         "&current=temperature_2m,precipitation,weather_code,wind_speed_10m"
         "&hourly=precipitation_probability&forecast_hours=3";
}

static int64_t race_start(const F1Schedule &s) {
  for (size_t i = s.n; i > 0; i--)
    if (s.sessions[i - 1].kind == RaceKind::RACE)
      return s.sessions[i - 1].start;
  return s.n > 0 ? s.sessions[s.n - 1].start : 0;
}

bool f1_race_weekend(const F1Schedule &s, int64_t now) {
  if (!s.valid || s.n == 0)
    return false;
  return now >= s.sessions[0].start - kDay && now <= race_start(s) + 3 * kHour;
}

uint32_t f1_poll_ms(F1Read read, const F1Schedule &s, int64_t now) {
  const int64_t race = s.valid ? race_start(s) : 0;
  const bool weekend = f1_race_weekend(s, now);
  switch (read) {
    case F1Read::NEXT:
      return race > 0 && now >= race ? 30 * kMinuteMs : 360 * kMinuteMs;
    case F1Read::DRIVERS:
    case F1Read::CONSTRUCTORS:
    case F1Read::LAST_RESULTS:
      // Jolpica posts results an hour after the flag.
      if (race > 0 && now >= race + 90 * 60 && now <= race + 8 * kHour)
        return 15 * kMinuteMs;
      return weekend ? 60 * kMinuteMs : 360 * kMinuteMs;
    case F1Read::WEATHER:
      return weekend ? 15 * kMinuteMs : 180 * kMinuteMs;
    case F1Read::CIRCUIT:
    case F1Read::ATHLETE:
      return 24 * 60 * kMinuteMs;
    default:
      return kMinuteMs;
  }
}

uint32_t f1_header_poll_ms(const F1Live &live, int64_t now) { return racing_poll_ms(live.snap, now); }

// Sorted; scripts/build_circuits.py's STOP is the same.
static const char *const kCircuitStop[] = {
    "and",           "autodrome",      "autodromo", "circuit", "circuito", "course", "da",     "de",
    "del",           "di",             "do",        "e",       "grand",    "internacional",    "international",
    "internazionale", "nazionale",     "of",        "prix",    "race",     "racing", "street", "the",
    "y"};

std::string circuit_key(const std::string &name) {
  std::string folded = ascii_fold(name), out, word;
  auto flush = [&out, &word]() {
    if (word.empty())
      return;
    bool stop = false;
    for (const char *s : kCircuitStop)
      if (word == s)
        stop = true;
    if (!stop) {
      if (!out.empty())
        out += ' ';
      out += word;
    }
    word.clear();
  };
  for (char ch : folded) {
    unsigned char c = (unsigned char) ch;
    if (isalnum(c))
      word += (char) tolower(c);
    else
      flush();
  }
  flush();
  return out;
}

const CircuitInfo *circuit_by_espn(uint32_t espn_id) {
  if (espn_id == 0)
    return nullptr;
  for (const CircuitInfo &c : kCircuits)
    if (c.espn_id == espn_id)
      return &c;
  return nullptr;
}

const CircuitInfo *circuit_by_jolpica(const std::string &id) {
  if (id.empty())
    return nullptr;
  for (const CircuitInfo &c : kCircuits)
    if (id == c.jolpica)
      return &c;
  return nullptr;
}

// Every word of part is a word of whole.
static bool words_in(const std::string &part, const std::string &whole) {
  const std::string padded = " " + whole + " ";
  size_t from = 0;
  while (from < part.size()) {
    size_t sp = part.find(' ', from);
    std::string w = part.substr(from, sp == std::string::npos ? std::string::npos : sp - from);
    if (padded.find(" " + w + " ") == std::string::npos)
      return false;
    if (sp == std::string::npos)
      break;
    from = sp + 1;
  }
  return true;
}

const CircuitInfo *circuit_by_name(const std::string &name) {
  const std::string key = circuit_key(name);
  if (key.empty())
    return nullptr;
  const CircuitInfo *best = nullptr;
  size_t best_len = 0;
  for (const CircuitInfo &c : kCircuits) {
    const char *p = c.names;
    while (*p != '\0') {
      const char *bar = strchr(p, '|');
      std::string k(p, bar != nullptr ? (size_t) (bar - p) : strlen(p));
      if (k == key)
        return &c;
      if (!k.empty() && k.size() > best_len && words_in(k, key)) {
        best = &c;
        best_len = k.size();
      }
      if (bar == nullptr)
        break;
      p = bar + 1;
    }
  }
  return best;
}

const CircuitInfo *circuit_for(const F1Schedule &s) {
  const CircuitInfo *c = circuit_by_jolpica(s.circuit_id);
  return c != nullptr ? c : circuit_by_name(s.circuit);
}

size_t f1_shape(const CircuitInfo &c, int16_t x, int16_t y, int16_t w, int16_t h, F1Point *out, size_t cap) {
  if (w < 2 || h < 2 || cap == 0 || c.n == 0 || (size_t) c.at + c.n > sizeof(kCircuitPoints) / 2)
    return 0;
  const int32_t sw = c.w > 0 ? c.w : 1, sh = c.h > 0 ? c.h : 1;
  // The box's tighter side sets the scale num / den.
  int32_t num = w - 1, den = sw;
  if ((int32_t) (h - 1) * sw < (int32_t) (w - 1) * sh) {
    num = h - 1;
    den = sh;
  }
  const int32_t bw = sw * num / den, bh = sh * num / den;
  const int32_t ox = x + (w - 1 - bw) / 2, oy = y + (h - 1 - bh) / 2;
  size_t n = 0;
  const uint8_t *p = kCircuitPoints + 2 * c.at;
  for (size_t i = 0; i < c.n && n < cap; i++) {
    F1Point q{(int16_t) (ox + (p[2 * i] * num + den / 2) / den), (int16_t) (oy + (p[2 * i + 1] * num + den / 2) / den)};
    if (n > 0 && out[n - 1].x == q.x && out[n - 1].y == q.y)
      continue;
    out[n++] = q;
  }
  if (c.closed && n > 1 && n < cap && (out[0].x != out[n - 1].x || out[0].y != out[n - 1].y))
    out[n++] = out[0];
  return n;
}

// ESPN's header colors, by Jolpica's constructorId.
static const F1Team kF1Teams[] = {
    {"mercedes", "Mercedes", "Mercedes", "MER", 0x00D2BE},
    {"ferrari", "Ferrari", "Ferrari", "FER", 0xDC0000},
    {"red_bull", "Red Bull", "Red Bull", "RBR", 0x00327D},
    {"mclaren", "McLaren", "McLaren", "MCL", 0xFF8700},
    {"aston_martin", "Aston Martin", "Aston Martin", "AMR", 0x006F62},
    {"alpine", "Alpine", "Alpine", "ALP", 0xFFF500},
    {"williams", "Williams", "Williams", "WIL", 0xFFFFFF},
    {"rb", "Racing Bulls", "Racing Bulls", "RB", 0x6692FF},
    {"audi", "Audi", "Audi", "AUD", 0xFF2D00},
    {"haas", "Haas", "Haas", "HAA", 0x5A5A5A},
    {"cadillac", "Cadillac", "Cadillac", "CAD", 0xA2AAAD},
};

static bool same_ci(const std::string &a, const char *b) {
  size_t n = strlen(b);
  if (a.size() != n)
    return false;
  for (size_t i = 0; i < n; i++)
    if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]))
      return false;
  return true;
}

const F1Team *f1_team(const std::string &id) {
  if (id.empty())
    return nullptr;
  for (const F1Team &t : kF1Teams)
    if (same_ci(id, t.id) || same_ci(id, t.espn) || same_ci(id, t.name))
      return &t;
  return nullptr;
}

static uint32_t lifted(uint32_t rgb) { return esphome::gameday::lift_color(rgb, 0xC0); }

uint32_t f1_team_color(const std::string &id) {
  const F1Team *t = f1_team(id);
  return t != nullptr ? lifted(t->rgb) : 0x808080;
}

static uint32_t car_color(const F1Car &c) { return c.rgb != 0 ? lifted(c.rgb) : f1_team_color(c.team); }

std::string f1_code(const std::string &last_name) {
  std::string out;
  for (char ch : ascii_fold(last_name)) {
    if (isalpha((unsigned char) ch))
      out += (char) toupper((unsigned char) ch);
    if (out.size() == 3)
      break;
  }
  return out;
}

void f1_live_codes(F1Live &live, const F1Standings &drivers) {
  if (!drivers.valid || !drivers.drivers)
    return;
  for (size_t i = 0; i < live.snap.race.rows.size() && i < kRaceRows; i++)
    for (size_t k = 0; k < drivers.n; k++)
      if (drivers.rows[k].code[0] != '\0' && same_ci(live.snap.race.rows[i].name, drivers.rows[k].name)) {
        put(live.cars[i].code, sizeof(live.cars[i].code), drivers.rows[k].code);
        break;
      }
}

std::string f1_fit(const std::string &s, size_t max) {
  size_t glyphs = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (((unsigned char) s[i] & 0xC0) == 0x80)
      continue;
    if (glyphs++ == max)
      return s.substr(0, i);
  }
  return s;
}

static size_t glyphs(const std::string &s) {
  size_t n = 0;
  for (char c : s)
    if (((unsigned char) c & 0xC0) != 0x80)
      n++;
  return n;
}

// Halves away from zero.
static long round10(long v) { return v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10); }

std::string f1_temp_text(int16_t c10, const F1Units &u) {
  long v = u.fahrenheit ? (long) c10 * 9 / 5 + 320 : c10;
  return std::to_string(round10(v)) + "\xC2\xB0" + (u.fahrenheit ? "F" : "C");
}

std::string f1_wind_text(int16_t kmh10, const F1Units &u) {
  long v = u.mph ? (long) kmh10 * 1000 / 1609 : kmh10;
  return std::to_string(round10(v)) + (u.mph ? " mph" : " km/h");
}

std::string f1_length_text(uint32_t meters, const F1Units &u) {
  uint64_t milli = u.mph ? (uint64_t) meters * 1000000 / 1609344 : meters;
  char buf[24];
  snprintf(buf, sizeof(buf), "%u.%03u %s", (unsigned) (milli / 1000), (unsigned) (milli % 1000), u.mph ? "mi" : "km");
  return buf;
}

struct WeatherWord {
  uint8_t from, to;
  const char *word, *long_word;
};
static const WeatherWord kWeather[] = {
    {0, 0, "Clear", "Clear sky"},          {1, 1, "Fair", "Mainly clear"},
    {2, 2, "Clouds", "Partly cloudy"},     {3, 3, "Cloudy", "Overcast"},
    {45, 45, "Fog", "Fog"},                {48, 48, "Fog", "Freezing fog"},
    {51, 51, "Drizzle", "Light drizzle"},  {53, 53, "Drizzle", "Drizzle"},
    {55, 55, "Drizzle", "Heavy drizzle"},  {56, 57, "Ice", "Freezing drizzle"},
    {61, 61, "Rain", "Light rain"},        {63, 63, "Rain", "Rain"},
    {65, 65, "Rain", "Heavy rain"},        {66, 67, "Ice", "Freezing rain"},
    {71, 71, "Snow", "Light snow"},        {73, 73, "Snow", "Snow"},
    {75, 75, "Snow", "Heavy snow"},        {77, 77, "Snow", "Snow grains"},
    {80, 80, "Showers", "Light showers"},  {81, 81, "Showers", "Showers"},
    {82, 82, "Showers", "Heavy showers"},  {85, 86, "Snow", "Snow showers"},
    {95, 95, "Storm", "Thunderstorm"},     {96, 99, "Storm", "Storm and hail"},
};

static const WeatherWord *weather_of(uint8_t code) {
  for (const WeatherWord &w : kWeather)
    if (code >= w.from && code <= w.to)
      return &w;
  return nullptr;
}

std::string f1_weather_word(uint8_t code) {
  const WeatherWord *w = weather_of(code);
  return w != nullptr ? w->word : "";
}

std::string f1_weather_long(uint8_t code) {
  const WeatherWord *w = weather_of(code);
  return w != nullptr ? w->long_word : "";
}

uint32_t f1_weather_color(uint8_t code) {
  if (code >= 95)
    return kAmber;
  if (code >= 51)
    return kBlue;
  if (code <= 1)
    return kGold;
  return kWhite;
}

std::string f1_countdown(int64_t seconds, size_t max) {
  if (seconds <= 0)
    return "Now";
  const int64_t d = seconds / kDay, h = seconds % kDay / kHour, m = seconds % kHour / 60, s = seconds % 60;
  char buf[32];
  if (d > 0) {
    snprintf(buf, sizeof(buf), "%lldd %lldh", (long long) d, (long long) h);
    if (strlen(buf) > max)
      snprintf(buf, sizeof(buf), "%lldd", (long long) d);
  } else if (h > 0) {
    snprintf(buf, sizeof(buf), "%lldh %lldm", (long long) h, (long long) m);
    if (strlen(buf) > max)
      snprintf(buf, sizeof(buf), "%lldh", (long long) h);
  } else {
    snprintf(buf, sizeof(buf), "%lld:%02lld", (long long) m, (long long) s);
    if (strlen(buf) > max)
      snprintf(buf, sizeof(buf), "%lldm", (long long) (m > 0 ? m : 1));
  }
  return f1_fit(buf, max);
}

static std::string count_word(int64_t n, const char *one) {
  return std::to_string(n) + " " + one + (n == 1 ? "" : "s");
}

std::string f1_countdown_long(int64_t seconds) {
  if (seconds <= 0)
    return "now";
  const int64_t d = seconds / kDay, h = seconds % kDay / kHour, m = seconds % kHour / 60;
  if (d > 0)
    return "in " + count_word(d, "day") + (h > 0 ? " " + count_word(h, "hour") : "");
  if (h > 0)
    return "in " + count_word(h, "hour") + (m > 0 ? " " + count_word(m, "minute") : "");
  return "in " + count_word(m > 0 ? m : 1, "minute");
}

std::string f1_session_when(const struct tm &start, const struct tm &now, size_t max) {
  static const char *const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const std::string day = day_label(start, now), at = golf_tee_label(start);
  if (day.empty() || at.empty())
    return f1_fit(at, max);
  const std::string wd = kDays[start.tm_wday];
  if (max >= 16 && day != "Today" && day != "Tomorrow") {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %s %d %s", wd.c_str(), kMonths[start.tm_mon], start.tm_mday, at.c_str());
    if (strlen(buf) <= max)
      return buf;
  }
  for (const std::string &t : {day + " " + at, wd + " " + at, wd.substr(0, 2) + " " + at})
    if (t.size() <= max)
      return t;
  return f1_fit(at, max);
}

std::string f1_points_text(int32_t p10) {
  char buf[16];
  if (p10 % 10 == 0)
    snprintf(buf, sizeof(buf), "%ld", (long) (p10 / 10));
  else
    snprintf(buf, sizeof(buf), "%s%ld.%ld", p10 < 0 ? "-" : "", (long) (labs(p10) / 10), (long) (labs(p10) % 10));
  return buf;
}

// "+1 Lap", "+3 Laps": the laps; 0 otherwise.
static int laps_behind(const char *status) {
  if (status[0] != '+')
    return 0;
  int n = atoi(status + 1);
  return n > 0 && n < 1000 ? n : 0;
}

static std::string gap_cut(const std::string &gap, size_t max) {
  std::string g = f1_fit(gap, max);
  while (!g.empty() && g.back() == '.')
    g.pop_back();
  return g;
}

std::string f1_result_detail(const F1Result &r, uint16_t leader_laps, size_t max) {
  const std::string pt = r.pos_text, status = r.status;
  std::string out;
  if (pt == "D" || status == "Disqualified")
    out = "DSQ";
  else if (pt == "W" || status == "Did not start" || status == "Withdrew")
    out = "DNS";
  else if (pt == "E")
    out = "EXC";
  else if (pt == "F")
    out = "DNQ";
  else if (pt == "N")
    out = "NC";
  else if (pt == "R" || pt.empty() || !isdigit((unsigned char) pt[0]))
    out = "DNF";
  if (!out.empty())
    return f1_fit(out, max);
  int down = laps_behind(r.status);
  if (down == 0 && leader_laps > 0 && r.laps > 0 && r.laps < leader_laps)
    down = leader_laps - r.laps;
  if (down > 0) {
    std::string longer = "+" + count_word(down, "lap");
    return longer.size() <= max ? longer : f1_fit("+" + std::to_string(down) + "L", max);
  }
  const std::string t = r.time;
  if (r.pos == 1 && !t.empty() && t[0] != '+')
    return t.size() <= max ? t : f1_fit("Winner", max);
  if (!t.empty() && t[0] == '+')
    return gap_cut(t, max);
  return "";
}

static F1Col col(int x, int w, F1Align a = F1Align::LEFT) {
  F1Col c;
  c.x = (int16_t) x;
  c.w = (int16_t) w;
  c.chars = (uint8_t) (w / 5);
  c.align = a;
  return c;
}

F1Geom f1_geom(F1Layout layout, int width) {
  if (width < 64)
    width = 64;
  const int cls = width >= 256 ? 256 : width >= 128 ? 128 : 64;
  const int o = (width - cls) / 2;
  const F1Align R = F1Align::RIGHT;
  F1Geom g;
  g.w = (int16_t) width;
  if (cls == 64) {
    g.status = col(0, width, F1Align::CENTER);
  } else {
    g.title = col(2, width - 4);
    g.title.chars = (uint8_t) ((width - 2) / 5);
    g.status = col(0, width - 2, R);
    g.status.chars = g.title.chars;
  }
  g.rows = (uint8_t) kF1Rows;
  switch (layout) {
    case F1Layout::TABLE:
      if (cls == 64) {
        g.pos = col(o, 10, R);
        g.mark = col(o + 11, 2);
        g.name = col(o + 14, 15);
        g.detail = col(o + 34, 30, R);
      } else if (cls == 128) {
        g.pos = col(o + 2, 10, R);
        g.mark = col(o + 13, 2);
        g.name = col(o + 16, 60);
        g.value = col(o + 77, 15, R);
        g.detail = col(o + 94, 30, R);
      } else {
        g.pos = col(o + 2, 10, R);
        g.mark = col(o + 14, 2);
        g.name = col(o + 18, 80);
        g.value = col(o + 102, 75);
        g.detail = col(o + 184, 70, R);
      }
      break;
    case F1Layout::LIST:
      if (cls == 64) {
        g.name = col(o + 1, 20);
        g.detail = col(o + 24, 40, R);
      } else if (cls == 128) {
        g.name = col(o + 2, 60);
        g.detail = col(o + 64, 62, R);
      } else {
        g.name = col(o + 2, 120);
        g.detail = col(o + 130, 124, R);
      }
      break;
    case F1Layout::SHAPE:
      g.shape_y = 9;
      g.shape_h = 46;
      if (cls == 64) {
        g.shape_x = (int16_t) (o + 2);
        g.shape_w = 60;
        g.rows = 0;
      } else if (cls == 128) {
        g.shape_x = (int16_t) (o + 1);
        g.shape_w = 60;
        g.name = col(o + 64, 62);
      } else {
        g.shape_x = (int16_t) (o + 2);
        g.shape_w = 100;
        g.name = col(o + 108, 60);
        g.detail = col(o + 170, 84, R);
      }
      break;
  }
  return g;
}

static bool narrow(const F1Geom &g) { return g.title.w == 0; }

// The status, then fit's title in the room left.
template<typename Fit>
static void title_row(F1Frame &f, const F1Geom &g, const std::string &status, uint32_t status_color, Fit fit) {
  f.status = f1_fit(status, g.status.chars);
  f.status_color = status_color;
  if (narrow(g))
    return;
  const size_t used = glyphs(f.status);
  const size_t room = used == 0 ? g.title.chars : used + 1 < g.title.chars ? g.title.chars - used - 1 : 0;
  if (room > 0)
    f.title = f1_fit(fit(room), room);
}

static F1Row &add_row(F1Frame &f) {
  F1Row &r = f.rows[f.n++];
  r = F1Row{};
  return r;
}

static bool race_like(RaceKind k) { return k == RaceKind::RACE || k == RaceKind::SPRINT; }

bool f1_live_has_board(const F1Live &live) {
  const GameSnapshot &s = live.snap;
  return s.valid && s.sport == Sport::RACING && !s.race.rows.empty() &&
         (s.race.phase == RacePhase::LIVE || s.race.phase == RacePhase::BREAK || s.race.phase == RacePhase::FINAL);
}

F1Frame f1_live_frame(const F1Live &live, int width, const std::string &when) {
  const GameSnapshot &s = live.snap;
  F1Frame f;
  f.screen = F1Screen::LIVE;
  f.layout = F1Layout::TABLE;
  f.width = (int16_t) width;
  const F1Geom g = f1_geom(F1Layout::TABLE, width);
  title_row(f, g, racing_state_text(s), racing_state_color(s),
            [&s](size_t room) { return racing_event_name(s.game_name, room); });
  f.ticker = racing_status_text(s, TickerOptions{}, when);
  if (!f1_live_has_board(live))
    return f;
  const bool race = race_like(s.race.kind);
  for (size_t i = 0; i < s.race.rows.size() && i < kRaceRows && f.n < g.rows; i++) {
    const RaceRow &rr = s.race.rows[i];
    const F1Car &car = live.cars[i];
    F1Row &r = add_row(f);
    put(r.pos, sizeof(r.pos), racing_pos_text(rr));
    r.mark = car_color(car);
    r.name_color = rr.winner ? kGold : kWhite;
    const std::string code = car.code[0] != '\0' ? std::string(car.code) : f1_code(rr.name);
    put(r.name, sizeof(r.name), g.name.chars <= 3 ? code : racing_name_fit(rr.name, g.name.chars));
    std::string extra = race ? (car.has_pits ? "P" + std::to_string(car.pits) : "")
                             : (car.laps > 0 ? std::to_string(car.laps) : "");
    if (g.value.chars >= 10) {
      const F1Team *t = f1_team(car.team);
      put(r.value, sizeof(r.value), f1_fit(t != nullptr ? t->name : car.team, g.value.chars));
      r.value_color = r.mark;
      std::string d = racing_detail_long(rr);
      if (!extra.empty()) {
        std::string tail = race ? extra : "L" + extra;
        if (glyphs(d) + 1 + tail.size() <= g.detail.chars)
          d = d.empty() ? tail : d + " " + tail;
      }
      put(r.detail, sizeof(r.detail), f1_fit(d, g.detail.chars));
    } else {
      if (g.value.chars > 0)
        put(r.value, sizeof(r.value), f1_fit(extra, g.value.chars));
      put(r.detail, sizeof(r.detail), f1_fit(racing_detail_text(rr), g.detail.chars));
    }
    if (rr.out != 0)
      r.detail_color = kRed;
  }
  for (size_t i = 0; i < s.race.rows.size() && i < kRaceRows; i++)
    f.spans.push_back({s.race.rows[i].name, car_color(live.cars[i])});
  return f;
}

// "FP1", "Qual", "SQ", "Spr", "Race" in 4.
static std::string session_short(const F1Session &s, size_t max) {
  std::string label = racing_session_label(s.kind, s.number);
  if (label.size() <= max)
    return label;
  if (s.kind == RaceKind::SPRINT_QUALIFYING)
    return "SQ";
  if (s.kind == RaceKind::SPRINT)
    return "Spr";
  return f1_fit(label, max);
}

F1Frame f1_next_frame_tm(const F1Schedule &s, int64_t now, const struct tm *local, const struct tm &now_local,
                         int width) {
  F1Frame f;
  f.screen = F1Screen::NEXT;
  f.layout = F1Layout::LIST;
  f.width = (int16_t) width;
  const F1Geom g = f1_geom(F1Layout::LIST, width);
  int next = -1;
  for (size_t i = 0; i < s.n && next < 0; i++)
    if (s.sessions[i].start > now)
      next = (int) i;
  std::string status;
  uint32_t color = kWhite;
  if (next >= 0) {
    const int64_t until = s.sessions[next].start - now;
    status = f1_countdown(until, narrow(g) ? g.status.chars : 7);
    color = until <= kHour ? kAmber : kWhite;
  } else if (s.valid && now <= race_start(s) + 3 * kHour) {
    status = "Race on";
    color = kGold;
  }
  title_row(f, g, status, color, [&s](size_t room) { return racing_event_name(s.race, room); });
  for (size_t i = 0; i < s.n && f.n < g.rows; i++) {
    const F1Session &ss = s.sessions[i];
    F1Row &r = add_row(f);
    std::string label = g.name.chars >= 16 ? racing_session_long(ss.kind, ss.number)
                                           : session_short(ss, g.name.chars);
    put(r.name, sizeof(r.name), f1_fit(label, g.name.chars));
    std::string at = ss.timed ? f1_session_when(local[i], now_local, g.detail.chars) : day_label(local[i], now_local);
    put(r.detail, sizeof(r.detail), f1_fit(at, g.detail.chars));
    const bool past = ss.start <= now;
    r.name_color = past ? kDim : (int) i == next ? kGold : kWhite;
    r.detail_color = past ? kDim : (int) i == next ? kWhite : kGray;
  }
  std::string t = s.race;
  std::string where = s.circuit;
  if (s.locality[0] != '\0')
    where += std::string(where.empty() ? "" : ", ") + s.locality;
  if (s.country[0] != '\0')
    where += std::string(where.empty() ? "" : ", ") + s.country;
  add_part(t, where);
  for (size_t i = 0; i < s.n; i++) {
    if (s.sessions[i].start <= now)
      continue;
    const std::string label = racing_session_long(s.sessions[i].kind, s.sessions[i].number);
    add_part(t, label + " " + (s.sessions[i].timed ? racing_when(local[i], now_local) : day_label(local[i], now_local)));
  }
  if (next >= 0)
    add_part(t, racing_session_long(s.sessions[next].kind, s.sessions[next].number) + " " +
                    f1_countdown_long(s.sessions[next].start - now));
  f.ticker = t;
  return f;
}

F1Frame f1_circuit_frame(const CircuitInfo *c, const F1CircuitFacts &facts, const F1Units &u, int width) {
  F1Frame f;
  f.screen = F1Screen::CIRCUIT;
  f.layout = F1Layout::SHAPE;
  f.width = (int16_t) width;
  const F1Geom g = f1_geom(F1Layout::SHAPE, width);
  const std::string full = facts.valid ? std::string(facts.name) : c != nullptr ? std::string(c->name) : "";
  const std::string brief = c != nullptr ? std::string(c->name) : full;
  if (narrow(g)) {
    title_row(f, g, glyphs(full) <= g.status.chars ? full : brief, kWhite, [](size_t) { return std::string(); });
  } else {
    title_row(f, g, "", kWhite, [&full, &brief](size_t room) {
      return glyphs(full) <= room ? full : glyphs(brief) <= room ? brief : whole_words(full, room);
    });
  }
  if (c != nullptr)
    f.shape_n = (uint8_t) f1_shape(*c, g.shape_x, g.shape_y, g.shape_w, g.shape_h, f.shape, kF1ShapeMax);
  const uint32_t len = facts.valid && facts.length_m > 0 ? facts.length_m : c != nullptr ? c->length_m : 0;
  const unsigned laps = facts.valid && facts.laps > 0 ? facts.laps : c != nullptr ? c->laps : 0;
  const unsigned turns = facts.valid && facts.turns > 0 ? facts.turns : c != nullptr ? c->turns : 0;
  const std::string rec = facts.valid ? facts.record : "";
  const std::string holder = facts.record_name[0] != '\0' ? facts.record_name : facts.record_code;
  const std::string year = facts.record_year > 0 ? std::to_string(facts.record_year) : "";
  const bool labels = g.detail.w > 0;
  auto row = [&f, &g, labels](const std::string &label, const std::string &value, const std::string &brief_text) {
    if (f.n >= g.rows || g.name.w == 0)
      return;
    F1Row &r = add_row(f);
    if (labels) {
      put(r.name, sizeof(r.name), f1_fit(label, g.name.chars));
      put(r.detail, sizeof(r.detail), f1_fit(value, g.detail.chars));
      r.name_color = kGray;
      r.detail_color = kWhite;
    } else {
      put(r.name, sizeof(r.name), f1_fit(brief_text, g.name.chars));
    }
  };
  if (len > 0)
    row("Length", f1_length_text(len, u), f1_length_text(len, u));
  if (laps > 0)
    row("Laps", std::to_string(laps), std::to_string(laps) + " laps");
  if (turns > 0)
    row("Turns", std::to_string(turns), std::to_string(turns) + " turns");
  if (!rec.empty()) {
    row("Lap record", rec, "Rec " + rec);
    std::string code = facts.record_code[0] != '\0' ? std::string(facts.record_code) : f1_code(holder);
    if (!holder.empty())
      row("Set by", year.empty() ? holder : holder + " " + year, year.empty() ? code : code + " " + year);
    else if (!year.empty())
      row("Set in", year, "Set in " + year);
  }
  if (facts.valid && facts.has_direction)
    row("Direction", facts.clockwise ? "Clockwise" : "Anticlockwise", "");
  if (f.n > 0 && f.rows[f.n - 1].name[0] == '\0' && f.rows[f.n - 1].detail[0] == '\0')
    f.n--;
  std::string t = full;
  if (facts.valid && facts.city[0] != '\0')
    t += std::string(", ") + facts.city + (facts.country[0] != '\0' ? std::string(", ") + facts.country : "");
  std::string nums;
  if (len > 0)
    nums = f1_length_text(len, u);
  if (laps > 0)
    nums += (nums.empty() ? "" : ", ") + std::to_string(laps) + " laps";
  if (turns > 0)
    nums += (nums.empty() ? "" : ", ") + std::to_string(turns) + " turns";
  add_part(t, nums);
  if (facts.valid && facts.has_direction)
    add_part(t, facts.clockwise ? "Runs clockwise" : "Runs anticlockwise");
  if (!rec.empty()) {
    std::string r = "Lap record " + rec;
    if (!holder.empty())
      r += ", " + holder;
    if (!year.empty())
      r += ", " + year;
    add_part(t, r);
  }
  f.ticker = t;
  return f;
}

F1Frame f1_weather_frame(const F1Weather &w, const std::string &place, const F1Units &u, int width) {
  F1Frame f;
  f.screen = F1Screen::WEATHER;
  f.layout = F1Layout::LIST;
  f.width = (int16_t) width;
  const F1Geom g = f1_geom(F1Layout::LIST, width);
  if (!w.valid) {
    title_row(f, g, "", kWhite, [&place](size_t room) { return whole_words(place, room); });
    return f;
  }
  const std::string temp = f1_temp_text(w.temp_c10, u), word = f1_weather_word(w.code);
  const uint32_t color = f1_weather_color(w.code);
  if (narrow(g)) {
    std::string both = word.empty() ? temp : temp + " " + word;
    title_row(f, g, glyphs(both) <= g.status.chars ? both : temp, kWhite, [](size_t) { return std::string(); });
  } else {
    title_row(f, g, word, color, [&place](size_t room) { return whole_words(place, room); });
  }
  const bool wide = g.name.chars >= 8;
  auto row = [&f, &g](const std::string &label, const std::string &value, uint32_t value_color) {
    if (f.n >= g.rows)
      return;
    F1Row &r = add_row(f);
    put(r.name, sizeof(r.name), f1_fit(label, g.name.chars));
    put(r.detail, sizeof(r.detail), f1_fit(value, g.detail.chars));
    r.name_color = kGray;
    r.detail_color = value_color;
  };
  row(wide ? "Air temp" : "Air", temp, kWhite);
  if (w.rain_pct >= 0)
    row(wide ? "Rain chance" : "Rain", std::to_string(w.rain_pct) + "%", w.rain_pct >= 50 ? kBlue : kWhite);
  row("Wind", f1_wind_text(w.wind_kmh10, u), kWhite);
  const std::string sky = f1_weather_long(w.code);
  if (!sky.empty())
    row("Sky", glyphs(sky) <= g.detail.chars ? sky : word, color);
  if (w.precip_mm10 > 0) {
    char buf[24];
    if (u.mph)
      snprintf(buf, sizeof(buf), "%.2f in", w.precip_mm10 / 254.0);
    else
      snprintf(buf, sizeof(buf), "%u.%u mm", (unsigned) (w.precip_mm10 / 10), (unsigned) (w.precip_mm10 % 10));
    row(wide ? "Rain now" : "Wet", buf, kBlue);
  }
  std::string t = "Weather at " + place;
  std::string now = sky.empty() ? temp : sky + ", " + temp;
  now += ", wind " + f1_wind_text(w.wind_kmh10, u);
  if (w.rain_pct >= 0)
    now += ", " + std::to_string(w.rain_pct) + "% chance of rain in the next 3 hours";
  add_part(t, now);
  f.ticker = t;
  return f;
}

F1Frame f1_standings_frame(const F1Standings &s, int width) {
  F1Frame f;
  f.screen = s.drivers ? F1Screen::DRIVERS : F1Screen::CONSTRUCTORS;
  f.layout = F1Layout::TABLE;
  f.width = (int16_t) width;
  const F1Geom g = f1_geom(F1Layout::TABLE, width);
  const std::string round = s.round > 0 ? std::to_string(s.round) : "";
  const bool wide = g.value.chars >= 10;
  if (narrow(g)) {
    title_row(f, g, s.drivers ? "Drivers" : "Teams", kWhite, [](size_t) { return std::string(); });
  } else {
    std::string status = round.empty() ? "" : wide ? "After round " + round : "Round " + round;
    title_row(f, g, status, kGray, [&s, wide](size_t room) {
      std::string t = s.drivers ? (wide ? "Driver standings" : "Drivers") : (wide ? "Constructor standings" : "Teams");
      if (wide && s.season > 0)
        t = std::to_string(s.season) + " " + t;
      return whole_words(t, room);
    });
  }
  if (!s.valid)
    return f;
  for (size_t i = 0; i < s.n && f.n < g.rows; i++) {
    const F1Standing &st = s.rows[i];
    F1Row &r = add_row(f);
    put(r.pos, sizeof(r.pos), st.pos > 0 && st.pos < 100 ? std::to_string(st.pos) : std::string("-"));
    r.mark = f1_team_color(st.team);
    r.name_color = st.pos == 1 ? kGold : kWhite;
    const F1Team *team = f1_team(st.team);
    std::string name = st.name;
    if (g.name.chars <= 3)
      name = s.drivers ? std::string(st.code) : team != nullptr ? team->code : f1_code(st.name);
    else if (s.drivers)
      name = racing_name_fit(st.name, g.name.chars);
    put(r.name, sizeof(r.name), f1_fit(name, g.name.chars));
    const std::string pts = f1_points_text(st.points10);
    if (wide) {
      if (s.drivers) {
        put(r.value, sizeof(r.value), f1_fit(team != nullptr ? team->name : st.team, g.value.chars));
        r.value_color = r.mark;
      } else if (st.wins > 0) {
        put(r.value, sizeof(r.value), count_word(st.wins, "win"));
      }
      put(r.detail, sizeof(r.detail), f1_fit(pts + " pts", g.detail.chars));
    } else {
      if (g.value.chars > 0 && st.wins > 0)
        put(r.value, sizeof(r.value), f1_fit(std::to_string(st.wins) + "W", g.value.chars));
      put(r.detail, sizeof(r.detail), f1_fit(pts, g.detail.chars));
    }
    r.detail_color = kWhite;
  }
  std::string t = s.drivers ? "Drivers' championship" : "Constructors' championship";
  if (!round.empty())
    t += " after round " + round;
  std::string list;
  for (size_t i = 0; i < s.n; i++) {
    const F1Standing &st = s.rows[i];
    if (!list.empty())
      list += ", ";
    list += std::to_string(st.pos) + " " + st.name + " " + f1_points_text(st.points10);
    f.spans.push_back({st.name, f1_team_color(st.team)});
  }
  add_part(t, list);
  f.ticker = t;
  return f;
}

F1Frame f1_results_frame(const F1Results &res, int width) {
  F1Frame f;
  f.screen = F1Screen::LAST_RACE;
  f.layout = F1Layout::TABLE;
  f.width = (int16_t) width;
  const F1Geom g = f1_geom(F1Layout::TABLE, width);
  title_row(f, g, "Last race", kGold, [&res](size_t room) { return racing_event_name(res.race, room); });
  if (!res.valid)
    return f;
  const bool wide = g.value.chars >= 10;
  for (size_t i = 0; i < res.n && f.n < g.rows; i++) {
    const F1Result &x = res.rows[i];
    F1Row &r = add_row(f);
    put(r.pos, sizeof(r.pos), x.pos > 0 && x.pos < 100 ? std::to_string(x.pos) : std::string("-"));
    r.mark = f1_team_color(x.team);
    r.name_color = x.pos == 1 ? kGold : kWhite;
    put(r.name, sizeof(r.name), g.name.chars <= 3 ? std::string(x.code) : racing_name_fit(x.name, g.name.chars));
    if (wide) {
      const F1Team *team = f1_team(x.team);
      put(r.value, sizeof(r.value), f1_fit(team != nullptr ? team->name : x.team, g.value.chars));
      r.value_color = r.mark;
    } else if (g.value.chars > 0 && x.points10 > 0) {
      put(r.value, sizeof(r.value), f1_fit(f1_points_text(x.points10), g.value.chars));
    }
    const std::string d = f1_result_detail(x, res.laps, g.detail.chars);
    put(r.detail, sizeof(r.detail), d);
    r.detail_color = d == "DNF" || d == "DNS" || d == "DSQ" ? kRed : kGray;
  }
  std::string t = res.race;
  add_part(t, "Result");
  std::string list;
  for (size_t i = 0; i < res.n; i++) {
    const F1Result &x = res.rows[i];
    if (!list.empty())
      list += ", ";
    list += (x.pos > 0 ? std::to_string(x.pos) : std::string("-")) + " " + x.name;
    const std::string d = f1_result_detail(x, res.laps, 16);
    if (!d.empty() && d != "Winner")
      list += " " + d;
    f.spans.push_back({x.name, f1_team_color(x.team)});
  }
  add_part(t, list);
  f.ticker = t;
  return f;
}

size_t f1_rotation(const F1Have &h, F1Screen *out, size_t cap) {
  size_t n = 0;
  auto add = [&n, out, cap](bool have, F1Screen s) {
    if (have && n < cap)
      out[n++] = s;
  };
  add(h.live, F1Screen::LIVE);
  if (h.on_track && h.live)
    return n;
  add(h.next, F1Screen::NEXT);
  add(h.circuit, F1Screen::CIRCUIT);
  add(h.weather, F1Screen::WEATHER);
  add(h.drivers, F1Screen::DRIVERS);
  add(h.constructors, F1Screen::CONSTRUCTORS);
  add(h.last_race, F1Screen::LAST_RACE);
  return n;
}

uint32_t f1_frame_ms(F1Screen s) {
  switch (s) {
    case F1Screen::LIVE:
      return 15000;
    case F1Screen::WEATHER:
      return 8000;
    default:
      return 10000;
  }
}

}
