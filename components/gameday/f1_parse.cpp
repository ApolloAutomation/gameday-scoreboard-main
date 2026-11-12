// F1 display reads: ESPN, Jolpica and Open-Meteo documents.
#include "f1.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace espn {
namespace detail {

static void put(char *dst, size_t cap, const std::string &s) { GolfRow::put(dst, cap, s); }

// A number sent as a number or as text ("56").
static long number(JsonVariantConst v, long def, long lo, long hi) {
  long n;
  if (v.is<long long>()) {
    long long x = v.as<long long>();
    n = x < lo ? lo : x > hi ? hi : (long) x;
    return n;
  }
  const char *s = v.as<const char *>();
  if (s == nullptr)
    return def;
  char *end = nullptr;
  n = strtol(s, &end, 10);
  if (end == s)
    return def;
  return n < lo ? lo : n > hi ? hi : n;
}

// "12.5" is 125; text or a number.
static int32_t tenths(JsonVariantConst v, bool *ok = nullptr) {
  double d;
  if (v.is<double>()) {
    d = v.as<double>();
  } else {
    const char *s = v.as<const char *>();
    char *end = nullptr;
    d = s != nullptr ? strtod(s, &end) : 0;
    if (s == nullptr || end == s) {
      if (ok != nullptr)
        *ok = false;
      return 0;
    }
  }
  if (!(d > -100000.0 && d < 100000.0)) {
    if (ok != nullptr)
      *ok = false;
    return 0;
  }
  if (ok != nullptr)
    *ok = true;
  return (int32_t) lround(d * 10);
}

// Degrees as text ("36.1147") times 10000.
static bool degrees_e4(JsonVariantConst v, int32_t &out) {
  const char *s = v.as<const char *>();
  double d;
  if (s != nullptr) {
    char *end = nullptr;
    d = strtod(s, &end);
    if (end == s)
      return false;
  } else if (v.is<double>()) {
    d = v.as<double>();
  } else {
    return false;
  }
  if (!(d >= -180.0 && d <= 180.0))
    return false;
  out = (int32_t) lround(d * 10000);
  return true;
}

// "6.116 km" or "0.526" miles as meters.
static uint32_t meters(const std::string &s) {
  char *end = nullptr;
  double d = strtod(s.c_str(), &end);
  if (end == s.c_str() || !(d > 0 && d < 100000))
    return 0;
  bool km = s.find("km") != std::string::npos;
  return (uint32_t) lround(d * (km ? 1000.0 : 1609.344));
}

// The digits after part in a $ref.
static uint32_t ref_id(const std::string &ref, const char *part) {
  size_t at = ref.find(part);
  if (at == std::string::npos)
    return 0;
  unsigned long n = strtoul(ref.c_str() + at + strlen(part), nullptr, 10);
  return n > 0xFFFFFFFFul ? 0 : (uint32_t) n;
}

static uint32_t hex_color(const std::string &s) {
  if (s.size() != 6)
    return 0;
  for (char c : s)
    if (!isxdigit((unsigned char) c))
      return 0;
  return (uint32_t) strtoul(s.c_str(), nullptr, 16);
}

void fill_f1_live_filter(JsonDocument &f) {
  fill_racing_filter(f);
  JsonObject c = f["sports"][0]["leagues"][0]["events"][0]["competitors"][0].as<JsonObject>();
  keep(c, {"laps", "pitsTaken", "startOrder"});
  keep(c["vehicle"].as<JsonObject>(), {"manufacturer", "teamColor"});
}

int f1_live_from_doc(JsonObjectConst root, F1Live &out) {
  GameSnapshot s;
  int found = racing_from_doc(root, s);
  if (found != 1)
    return found;
  F1Car cars[kRaceRows];
  bool done[kRaceRows] = {};
  const size_t n = s.race.rows.size() < kRaceRows ? s.race.rows.size() : kRaceRows;
  if (s.race.session_start != 0 && n > 0) {
    for (JsonVariantConst e : root["sports"][0]["leagues"][0]["events"].as<JsonArrayConst>()) {
      if (text(e, "id") != s.event_id || when(e, "date") != s.race.session_start)
        continue;
      for (JsonVariantConst c : field(e, "competitors").as<JsonArrayConst>()) {
        std::string name = ascii_fold(text(c, "lastName"));
        if (name.empty())
          name = ascii_fold(text(c, "displayName"));
        char key[sizeof(RaceRow::name)];
        put(key, sizeof(key), name);
        for (size_t i = 0; i < n; i++) {
          if (done[i] || strcmp(s.race.rows[i].name, key) != 0)
            continue;
          F1Car &car = cars[i];
          put(car.code, sizeof(car.code), f1_code(name));
          JsonVariantConst v = field(c, "vehicle");
          put(car.team, sizeof(car.team), ascii_fold(text(v, "manufacturer")));
          car.rgb = hex_color(text(v, "teamColor"));
          car.laps = (uint16_t) number(field(c, "laps"), 0, 0, 999);
          JsonVariantConst pits = field(c, "pitsTaken");
          car.has_pits = !pits.isNull();
          car.pits = (uint8_t) number(pits, 0, 0, 99);
          car.grid = (uint8_t) number(field(c, "startOrder"), 0, 0, 99);
          done[i] = true;
          break;
        }
      }
      break;
    }
  }
  out.snap = std::move(s);
  for (size_t i = 0; i < kRaceRows; i++)
    out.cars[i] = i < n ? cars[i] : F1Car{};
  return 1;
}

struct SessionKey {
  const char *key;
  RaceKind kind;
  uint8_t number;
};
static const SessionKey kSessionKeys[] = {
    {"FirstPractice", RaceKind::PRACTICE, 1},           {"SecondPractice", RaceKind::PRACTICE, 2},
    {"ThirdPractice", RaceKind::PRACTICE, 3},           {"SprintQualifying", RaceKind::SPRINT_QUALIFYING, 0},
    {"SprintShootout", RaceKind::SPRINT_QUALIFYING, 0}, {"Sprint", RaceKind::SPRINT, 0},
    {"Qualifying", RaceKind::QUALIFYING, 0},
};

void fill_f1_race_filter(JsonDocument &f) {
  JsonObject r = f["MRData"]["RaceTable"]["Races"].add<JsonObject>();
  keep(r, {"season", "round", "raceName", "date", "time"});
  JsonObject c = r["Circuit"].to<JsonObject>();
  keep(c, {"circuitId", "circuitName"});
  keep(c["Location"].to<JsonObject>(), {"lat", "long", "locality", "country"});
  for (const SessionKey &k : kSessionKeys)
    keep(r[k.key].to<JsonObject>(), {"date", "time"});
}

// "2026-11-20" and "00:30:00Z"; untimed without a time.
static int64_t start_of(JsonVariantConst v, bool &timed) {
  std::string date = text(v, "date"), t = text(v, "time");
  timed = !t.empty();
  if (date.empty())
    return 0;
  return parse_iso8601_z(date + "T" + (timed ? t : std::string("00:00")));
}

int f1_race_from_doc(JsonObjectConst root, F1Schedule &out) {
  JsonVariantConst table = root["MRData"]["RaceTable"];
  if (table.isNull())
    return -1;
  JsonVariantConst r = field(table, "Races")[0];
  if (r.isNull())
    return 0;
  F1Schedule s;
  s.season = (uint16_t) number(field(r, "season"), 0, 0, 9999);
  s.round = (uint8_t) number(field(r, "round"), 0, 0, 255);
  put(s.race, sizeof(s.race), ascii_fold(text(r, "raceName")));
  JsonVariantConst c = field(r, "Circuit");
  put(s.circuit_id, sizeof(s.circuit_id), text(c, "circuitId"));
  put(s.circuit, sizeof(s.circuit), ascii_fold(text(c, "circuitName")));
  JsonVariantConst loc = field(c, "Location");
  put(s.locality, sizeof(s.locality), ascii_fold(text(loc, "locality")));
  put(s.country, sizeof(s.country), ascii_fold(text(loc, "country")));
  s.has_pos = degrees_e4(field(loc, "lat"), s.lat_e4) && degrees_e4(field(loc, "long"), s.lon_e4);
  if (!s.has_pos)
    s.lat_e4 = s.lon_e4 = 0;
  auto add = [&s](RaceKind kind, uint8_t number, int64_t start, bool timed) {
    if (start == 0 || s.n >= kF1Sessions)
      return;
    size_t at = s.n++;
    for (; at > 0 && s.sessions[at - 1].start > start; at--)
      s.sessions[at] = s.sessions[at - 1];
    s.sessions[at] = F1Session{kind, number, start, timed};
  };
  bool timed = true;
  for (const SessionKey &k : kSessionKeys) {
    int64_t t = start_of(field(r, k.key), timed);
    add(k.kind, k.number, t, timed);
  }
  int64_t race = start_of(r, timed);
  if (race == 0)
    return -1;
  // The race last, after any session sharing its start.
  if (s.n >= kF1Sessions)
    s.n = kF1Sessions - 1;
  add(RaceKind::RACE, 0, race, timed);
  s.valid = true;
  out = s;
  return 1;
}

void fill_f1_standings_filter(JsonDocument &f, bool drivers) {
  JsonObject t = f["MRData"]["StandingsTable"].to<JsonObject>();
  keep(t, {"season", "round"});
  JsonObject l = t["StandingsLists"].add<JsonObject>();
  keep(l, {"season", "round"});
  JsonObject row = l[drivers ? "DriverStandings" : "ConstructorStandings"].add<JsonObject>();
  keep(row, {"position", "points", "wins"});
  if (drivers) {
    keep(row["Driver"].to<JsonObject>(), {"code", "familyName"});
    row["Constructors"].add<JsonObject>()["constructorId"] = true;
  } else {
    keep(row["Constructor"].to<JsonObject>(), {"constructorId", "name"});
  }
}

// "Haas F1 Team" is "Haas".
static std::string team_name(const std::string &id, const std::string &name) {
  const F1Team *t = f1_team(id);
  if (t != nullptr)
    return t->name;
  std::string n = ascii_fold(name);
  for (const char *tail : {" F1 Team", " Racing", " Team"}) {
    size_t len = strlen(tail);
    if (n.size() > len && n.compare(n.size() - len, len, tail) == 0) {
      n.erase(n.size() - len);
      break;
    }
  }
  return n;
}

int f1_standings_from_doc(JsonObjectConst root, bool drivers, F1Standings &out) {
  JsonVariantConst table = root["MRData"]["StandingsTable"];
  if (table.isNull())
    return -1;
  JsonVariantConst list = field(table, "StandingsLists")[0];
  if (list.isNull())
    return 0;
  F1Standings s;
  s.drivers = drivers;
  s.season = (uint16_t) number(field(list, "season"), number(field(table, "season"), 0, 0, 9999), 0, 9999);
  s.round = (uint8_t) number(field(list, "round"), number(field(table, "round"), 0, 0, 255), 0, 255);
  for (JsonVariantConst row : field(list, drivers ? "DriverStandings" : "ConstructorStandings").as<JsonArrayConst>()) {
    if (s.n >= kF1Keep)
      break;
    F1Standing &st = s.rows[s.n];
    st = F1Standing{};
    st.pos = (uint8_t) number(field(row, "position"), 0, 0, 255);
    st.points10 = tenths(field(row, "points"));
    st.wins = (uint8_t) number(field(row, "wins"), 0, 0, 255);
    if (drivers) {
      JsonVariantConst d = field(row, "Driver");
      std::string family = ascii_fold(text(d, "familyName"));
      std::string code = text(d, "code");
      put(st.name, sizeof(st.name), family);
      put(st.code, sizeof(st.code), code.size() == 3 ? code : f1_code(family));
      JsonArrayConst teams = field(row, "Constructors").as<JsonArrayConst>();
      if (teams.size() > 0)
        put(st.team, sizeof(st.team), text(teams[teams.size() - 1], "constructorId"));
    } else {
      JsonVariantConst c = field(row, "Constructor");
      std::string id = text(c, "constructorId");
      put(st.team, sizeof(st.team), id);
      put(st.name, sizeof(st.name), team_name(id, text(c, "name")));
      const F1Team *t = f1_team(id);
      if (t != nullptr)
        put(st.code, sizeof(st.code), t->code);
    }
    s.n++;
  }
  if (s.n == 0)
    return 0;
  s.valid = true;
  out = s;
  return 1;
}

void fill_f1_results_filter(JsonDocument &f) {
  JsonObject r = f["MRData"]["RaceTable"]["Races"].add<JsonObject>();
  keep(r, {"season", "round", "raceName", "date", "time"});
  r["Circuit"]["circuitId"] = true;
  JsonObject x = r["Results"].add<JsonObject>();
  keep(x, {"position", "positionText", "points", "grid", "laps", "status"});
  x["Time"]["time"] = true;
  keep(x["Driver"].to<JsonObject>(), {"code", "familyName"});
  x["Constructor"]["constructorId"] = true;
  x["FastestLap"]["rank"] = true;
}

int f1_results_from_doc(JsonObjectConst root, F1Results &out) {
  JsonVariantConst table = root["MRData"]["RaceTable"];
  if (table.isNull())
    return -1;
  JsonVariantConst r = field(table, "Races")[0];
  if (r.isNull())
    return 0;
  F1Results res;
  res.season = (uint16_t) number(field(r, "season"), 0, 0, 9999);
  res.round = (uint8_t) number(field(r, "round"), 0, 0, 255);
  put(res.race, sizeof(res.race), ascii_fold(text(r, "raceName")));
  put(res.circuit_id, sizeof(res.circuit_id), text(field(r, "Circuit"), "circuitId"));
  bool timed = true;
  res.start = start_of(r, timed);
  for (JsonVariantConst x : field(r, "Results").as<JsonArrayConst>()) {
    if (res.n >= kF1Keep)
      break;
    F1Result &row = res.rows[res.n];
    row = F1Result{};
    row.pos = (uint8_t) number(field(x, "position"), 0, 0, 255);
    put(row.pos_text, sizeof(row.pos_text), text(x, "positionText"));
    JsonVariantConst d = field(x, "Driver");
    std::string family = ascii_fold(text(d, "familyName"));
    std::string code = text(d, "code");
    put(row.name, sizeof(row.name), family);
    put(row.code, sizeof(row.code), code.size() == 3 ? code : f1_code(family));
    put(row.team, sizeof(row.team), text(field(x, "Constructor"), "constructorId"));
    put(row.time, sizeof(row.time), text(field(x, "Time"), "time"));
    put(row.status, sizeof(row.status), text(x, "status"));
    row.laps = (uint16_t) number(field(x, "laps"), 0, 0, 999);
    row.grid = (uint8_t) number(field(x, "grid"), 0, 0, 99);
    row.points10 = (int16_t) tenths(field(x, "points"));
    row.fastest = number(field(field(x, "FastestLap"), "rank"), 0, 0, 99) == 1;
    res.n++;
  }
  if (res.n == 0)
    return 0;
  res.laps = res.rows[0].laps;
  res.valid = true;
  out = res;
  return 1;
}

void fill_f1_circuit_filter(JsonDocument &f) {
  keep(f.to<JsonObject>(), {"id", "fullName", "length", "distance", "laps", "turns", "direction", "fastestLapTime",
                            "fastestLapYear"});
  keep(f["address"].to<JsonObject>(), {"city", "country"});
  f["fastestLapDriver"]["$ref"] = true;
}

int f1_circuit_from_doc(JsonObjectConst root, F1CircuitFacts &out) {
  long id = number(root["id"], 0, 0, 0x7FFFFFFF);
  std::string name = ascii_fold(text(root, "fullName"));
  if (id <= 0 || name.empty())
    return -1;
  F1CircuitFacts c;
  c.espn_id = (uint32_t) id;
  put(c.name, sizeof(c.name), name);
  JsonVariantConst addr = field(root, "address");
  put(c.city, sizeof(c.city), ascii_fold(text(addr, "city")));
  put(c.country, sizeof(c.country), ascii_fold(text(addr, "country")));
  uint32_t len = meters(text(root, "length"));
  c.length_m = (uint16_t) (len > 65535 ? 0 : len);
  c.distance_m = meters(text(root, "distance"));
  c.laps = (uint8_t) number(field(root, "laps"), 0, 0, 255);
  c.turns = (uint8_t) number(field(root, "turns"), 0, 0, 255);
  std::string dir = lower(text(root, "direction"));
  c.has_direction = !dir.empty();
  c.clockwise = dir.find("counter") == std::string::npos && dir.find("anti") == std::string::npos;
  std::string rec = text(root, "fastestLapTime");
  put(c.record, sizeof(c.record), rec);
  if (!rec.empty()) {
    c.record_year = (uint16_t) number(field(root, "fastestLapYear"), 0, 0, 9999);
    c.record_athlete = ref_id(text(field(root, "fastestLapDriver"), "$ref"), "/athletes/");
  }
  c.valid = true;
  out = c;
  return 1;
}

void fill_f1_athlete_filter(JsonDocument &f) {
  keep(f.to<JsonObject>(), {"id", "abbreviation", "lastName", "displayName"});
}

int f1_athlete_from_doc(JsonObjectConst root, F1Athlete &out) {
  std::string name = ascii_fold(text(root, "lastName"));
  if (name.empty()) {
    std::string full = ascii_fold(text(root, "displayName"));
    name = full.empty() ? full : last_word(full);
  }
  if (name.empty())
    return -1;
  F1Athlete a;
  put(a.name, sizeof(a.name), name);
  std::string code = text(root, "abbreviation");
  put(a.code, sizeof(a.code), code.size() == 3 ? code : f1_code(name));
  a.valid = true;
  out = a;
  return 1;
}

void fill_f1_weather_filter(JsonDocument &f) {
  keep(f.to<JsonObject>(), {"error"});
  keep(f["current"].to<JsonObject>(), {"time", "temperature_2m", "precipitation", "weather_code", "wind_speed_10m"});
  f["hourly"]["precipitation_probability"] = true;
}

int f1_weather_from_doc(JsonObjectConst root, F1Weather &out) {
  JsonVariantConst cur = root["current"];
  if (flag(root, "error") || cur.isNull())
    return -1;
  bool ok_t = false, ok_w = false;
  int32_t t = tenths(field(cur, "temperature_2m"), &ok_t);
  int32_t w = tenths(field(cur, "wind_speed_10m"), &ok_w);
  long code = number(field(cur, "weather_code"), -1, -1, 255);
  if (!ok_t || code < 0)
    return -1;
  F1Weather wx;
  wx.at = parse_iso8601_z(text(cur, "time"));
  wx.temp_c10 = (int16_t) (t < -1000 ? -1000 : t > 1000 ? 1000 : t);
  wx.wind_kmh10 = (int16_t) (!ok_w || w < 0 ? 0 : w > 5000 ? 5000 : w);
  int32_t p = tenths(field(cur, "precipitation"));
  wx.precip_mm10 = (uint16_t) (p < 0 ? 0 : p > 10000 ? 10000 : p);
  wx.code = (uint8_t) code;
  for (JsonVariantConst v : field(field(root, "hourly"), "precipitation_probability").as<JsonArrayConst>()) {
    long pct = number(v, -1, -1, 100);
    if (pct > wx.rain_pct)
      wx.rain_pct = (int8_t) pct;
  }
  wx.valid = true;
  out = wx;
  return 1;
}

}
}
