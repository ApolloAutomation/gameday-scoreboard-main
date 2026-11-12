// The F1 display's reads, text, circuits and frames.
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "board_layout.h"
#include "f1.h"
#include "panel_text.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define FCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

#define FCHECK_EQ(a, b)                                                                               \
  do {                                                                                                \
    checks++;                                                                                         \
    const std::string va = (a), vb = (b);                                                             \
    if (va != vb) {                                                                                   \
      failures++;                                                                                     \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                                 \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// UTC stands in for the panel's zone.
struct tm utc(int64_t epoch) {
  time_t t = (time_t) epoch;
  struct tm out {};
  gmtime_r(&t, &out);
  return out;
}

size_t glyphs(const std::string &s) {
  size_t n = 0;
  for (char c : s)
    if (((unsigned char) c & 0xC0) != 0x80)
      n++;
  return n;
}

const int kWidths[] = {64, 128, 192, 256};

F1Live live_of(const char *file) {
  F1Live live;
  std::string json = slurp(file);
  FCHECK(parse_f1_live(json, live) == 1, "%s parses", file);
  return live;
}

F1Schedule race_of(const char *file) {
  F1Schedule s;
  std::string json = slurp(file);
  FCHECK(parse_f1_race(json, s) == 1, "%s parses", file);
  return s;
}

F1Standings standings_of(const char *file, bool drivers) {
  F1Standings s;
  std::string json = slurp(file);
  FCHECK(parse_f1_standings(json, drivers, s) == 1, "%s parses", file);
  return s;
}

F1Results results_of() {
  F1Results r;
  std::string json = slurp("fixtures/jolpica_last_results.json");
  FCHECK(parse_f1_results(json, r) == 1, "last results parse");
  return r;
}

F1CircuitFacts circuit_of(const char *file) {
  F1CircuitFacts c;
  std::string json = slurp(file);
  FCHECK(parse_f1_circuit(json, c) == 1, "%s parses", file);
  return c;
}

F1Weather weather_of(const char *file) {
  F1Weather w;
  std::string json = slurp(file);
  FCHECK(parse_f1_weather(json, w) == 1, "%s parses", file);
  return w;
}

// Every part of a frame inside its geometry.
void check_fits(const F1Frame &f, const char *what) {
  const F1Geom g = f1_geom(f.layout, f.width);
  if (g.title.w == 0) {
    FCHECK(f.title.empty() && glyphs(f.status) <= g.status.chars, "%s %d: one cell \"%s\"", what, f.width,
           f.status.c_str());
  } else {
    const size_t used = glyphs(f.title) + glyphs(f.status) + (f.title.empty() || f.status.empty() ? 0 : 1);
    FCHECK(used <= g.title.chars, "%s %d: title row \"%s\" \"%s\"", what, f.width, f.title.c_str(), f.status.c_str());
  }
  FCHECK(f.n <= g.rows, "%s %d: %u rows", what, f.width, (unsigned) f.n);
  for (size_t i = 0; i < f.n; i++) {
    const F1Row &r = f.rows[i];
    FCHECK(glyphs(r.pos) <= g.pos.chars, "%s %d row %zu pos \"%s\"", what, f.width, i, r.pos);
    FCHECK(glyphs(r.name) <= g.name.chars, "%s %d row %zu name \"%s\"", what, f.width, i, r.name);
    FCHECK(glyphs(r.value) <= g.value.chars, "%s %d row %zu value \"%s\"", what, f.width, i, r.value);
    FCHECK(glyphs(r.detail) <= g.detail.chars, "%s %d row %zu detail \"%s\"", what, f.width, i, r.detail);
  }
  for (size_t i = 0; i < f.shape_n; i++)
    FCHECK(f.shape[i].x >= g.shape_x && f.shape[i].x < g.shape_x + g.shape_w && f.shape[i].y >= g.shape_y &&
               f.shape[i].y < g.shape_y + g.shape_h,
           "%s %d: point %zu in the box", what, f.width, i);
}

void test_urls() {
  FCHECK_EQ(f1_jolpica_url(F1Read::NEXT), "https://api.jolpi.ca/ergast/f1/current/next.json");
  FCHECK_EQ(f1_jolpica_url(F1Read::DRIVERS), "https://api.jolpi.ca/ergast/f1/current/driverStandings.json");
  FCHECK_EQ(f1_jolpica_url(F1Read::CONSTRUCTORS), "https://api.jolpi.ca/ergast/f1/current/constructorStandings.json");
  FCHECK_EQ(f1_jolpica_url(F1Read::LAST_RESULTS), "https://api.jolpi.ca/ergast/f1/current/last/results.json");
  FCHECK_EQ(f1_jolpica_url(F1Read::WEATHER), "");
  FCHECK_EQ(f1_round_url(22), "https://api.jolpi.ca/ergast/f1/current/22.json");
  FCHECK_EQ(f1_circuit_url(4250), "https://sports.core.api.espn.com/v2/sports/racing/leagues/f1/circuits/4250");
  FCHECK_EQ(f1_athlete_url(4665), "https://sports.core.api.espn.com/v2/sports/racing/athletes/4665");
  FCHECK_EQ(f1_weather_url(361147, -1151730),
            "https://api.open-meteo.com/v1/forecast?latitude=36.1147&longitude=-115.1730"
            "&current=temperature_2m,precipitation,weather_code,wind_speed_10m"
            "&hourly=precipitation_probability&forecast_hours=3");
  FCHECK(f1_weather_url(-237036, -466997).find("latitude=-23.7036&longitude=-46.6997") != std::string::npos,
         "south and west");
  FCHECK(f1_weather_url(-5, 12).find("latitude=-0.0005&longitude=0.0012") != std::string::npos, "near zero");
}

void test_live() {
  F1Live live = live_of("fixtures/racing_header_f1_live.json");
  const Racing &r = live.snap.race;
  FCHECK(r.phase == RacePhase::LIVE && r.kind == RaceKind::RACE && r.rows.size() == kRaceRows, "the race on track");
  FCHECK(f1_live_has_board(live), "a board");
  FCHECK_EQ(r.rows[0].name, "Antonelli");
  FCHECK_EQ(live.cars[0].code, "ANT");
  FCHECK_EQ(live.cars[0].team, "Mercedes");
  FCHECK(live.cars[0].rgb == 0x00D2BE && live.cars[0].pits == 1 && live.cars[0].has_pits && live.cars[0].grid == 1 &&
             live.cars[0].laps == 56,
         "Antonelli's car");
  FCHECK(live.cars[1].rgb == 0xDC0000 && live.cars[1].pits == 2 && live.cars[1].grid == 4, "Leclerc's car");
  for (size_t i = 0; i < r.rows.size(); i++)
    FCHECK(live.cars[i].code[0] != '\0' && live.cars[i].rgb != 0, "row %zu has its car", i);

  F1Frame f = f1_live_frame(live, 128, "");
  FCHECK_EQ(f.status, "Lap 34/56");
  FCHECK_EQ(f.title, "US GP");
  FCHECK(f.n == 6, "six rows");
  FCHECK_EQ(f.rows[0].name, "Antonelli");
  FCHECK_EQ(f.rows[0].value, "P1");
  FCHECK_EQ(f.rows[1].detail, "+13.07");
  FCHECK(f.rows[0].mark == 0x00D2BE, "Mercedes' color");
  FCHECK(f.rows[2].mark == esphome::gameday::lift_color(0x00327D, 0xC0), "Red Bull's lifted");
  FCHECK(f.spans.size() == kRaceRows && f.spans[0].text == "Antonelli", "the ticker's names in team colors");
  FCHECK(f.ticker.find("Antonelli") != std::string::npos, "the ticker lists the order");
  const std::string marked = esphome::gameday::ticker_markup(esphome::gameday::panel_text(f.ticker), f.spans);
  FCHECK(marked.find("`00D2BE Antonelli`") != std::string::npos, "the leader in Mercedes' color: %s", marked.c_str());

  F1Frame one = f1_live_frame(live, 64, "");
  FCHECK_EQ(one.status, "Lap 34/56");
  FCHECK_EQ(one.rows[0].name, "ANT");
  FCHECK_EQ(one.rows[1].detail, "+13.07");
  FCHECK_EQ(one.rows[1].value, "");
  F1Frame four = f1_live_frame(live, 256, "");
  FCHECK_EQ(four.rows[1].value, "Ferrari");
  FCHECK_EQ(four.rows[1].detail, "+13.078 P2");
  FCHECK_EQ(four.rows[0].detail, "P1");
  for (int w : kWidths)
    check_fits(f1_live_frame(live, w, "Today 2:00p"), "live");

  // Practice: laps run, the leader's own time
  F1Live prac = live_of("fixtures/racing_header_f1_practice.json");
  FCHECK(prac.snap.race.phase == RacePhase::BREAK && prac.snap.race.kind == RaceKind::PRACTICE, "after FP2");
  F1Frame p = f1_live_frame(prac, 128, "");
  FCHECK_EQ(p.status, "FP2 final");
  FCHECK_EQ(p.rows[0].value, "37");
  FCHECK_EQ(p.rows[0].detail, "1:10.7");
  FCHECK_EQ(f1_live_frame(prac, 256, "").rows[0].detail, "1:10.763 L37");
  for (int w : kWidths)
    check_fits(f1_live_frame(prac, w, ""), "practice");

  // A sprint shows pit stops
  F1Live sprint = live_of("fixtures/racing_header_f1_sprint.json");
  FCHECK(sprint.snap.race.kind == RaceKind::SPRINT && sprint.cars[0].pits == 3, "the sprint's pit stops");
  FCHECK_EQ(f1_live_frame(sprint, 128, "").rows[0].value, "P3");

  // Before the weekend: no board, a ticker still
  F1Live pre = live_of("fixtures/f1_header_vegas_pre.json");
  FCHECK(pre.snap.race.phase == RacePhase::PRE && !f1_live_has_board(pre), "Las Vegas to come");
  F1Frame pf = f1_live_frame(pre, 128, "Fri 12:30a");
  FCHECK(pf.n == 0 && pf.spans.empty(), "no rows");
  FCHECK_EQ(pf.status, "Upcoming");
  FCHECK(pf.ticker.find("Las Vegas") != std::string::npos, "the ticker names it");

  F1Live none;
  std::string empty = slurp("fixtures/racing_header_none.json");
  FCHECK(parse_f1_live(empty, none) == 0, "no session listed");
  std::string bad = "[1,2]";
  FCHECK(parse_f1_live(bad, none) == -1, "not a header");

  // Codes from the standings
  F1Standings fake;
  fake.valid = true;
  fake.n = 1;
  strcpy(fake.rows[0].name, "Leclerc");
  strcpy(fake.rows[0].code, "CLC");
  f1_live_codes(live, fake);
  FCHECK_EQ(live.cars[1].code, "CLC");
  FCHECK_EQ(live.cars[0].code, "ANT");
}

void test_race() {
  F1Schedule s = race_of("fixtures/jolpica_next.json");
  FCHECK(s.valid && s.season == 2026 && s.round == 21, "round 21");
  FCHECK_EQ(s.race, "Las Vegas Grand Prix");
  FCHECK_EQ(s.circuit_id, "vegas");
  FCHECK_EQ(s.circuit, "Las Vegas Strip Street Circuit");
  FCHECK_EQ(s.locality, "Las Vegas");
  FCHECK_EQ(s.country, "USA");
  FCHECK(s.has_pos && s.lat_e4 == 361147 && s.lon_e4 == -1151730, "where: %d %d", s.lat_e4, s.lon_e4);
  FCHECK(s.n == 5, "five sessions");
  const RaceKind kinds[] = {RaceKind::PRACTICE, RaceKind::PRACTICE, RaceKind::PRACTICE, RaceKind::QUALIFYING,
                            RaceKind::RACE};
  const int64_t starts[] = {1795134600, 1795147200, 1795221000, 1795233600, 1795320000};
  for (size_t i = 0; i < 5; i++)
    FCHECK(s.sessions[i].kind == kinds[i] && s.sessions[i].start == starts[i] && s.sessions[i].timed, "session %zu",
           i);
  FCHECK(s.sessions[2].number == 3, "FP3");

  F1Schedule sp = race_of("fixtures/jolpica_race_sprint.json");
  const RaceKind sk[] = {RaceKind::PRACTICE, RaceKind::SPRINT_QUALIFYING, RaceKind::SPRINT, RaceKind::QUALIFYING,
                         RaceKind::RACE};
  FCHECK(sp.n == 5, "a sprint weekend's five");
  for (size_t i = 0; i < 5 && i < sp.n; i++)
    FCHECK(sp.sessions[i].kind == sk[i], "sprint session %zu", i);
  FCHECK(circuit_for(sp) != nullptr && strcmp(circuit_for(sp)->key, "marina-bay") == 0, "Marina Bay");

  F1Schedule none;
  std::string empty = R"({"MRData":{"RaceTable":{"season":"2027","Races":[]}}})";
  FCHECK(parse_f1_race(empty, none) == 0 && !none.valid, "between seasons");
  std::string other = R"({"leagues":[]})";
  FCHECK(parse_f1_race(other, none) == -1, "not Jolpica's");
  std::string no_time =
      R"({"MRData":{"RaceTable":{"Races":[{"season":"1990","round":"3","raceName":"Grand Prix","date":"1990-05-13",)"
      R"("Circuit":{"circuitId":"imola","Location":{"lat":"x","long":"11.7"}}}]}}})";
  F1Schedule old;
  FCHECK(parse_f1_race(no_time, old) == 1 && old.n == 1 && !old.sessions[0].timed && !old.has_pos,
         "a race with a date alone");
}

void test_standings() {
  F1Standings d = standings_of("fixtures/jolpica_driver_standings.json", true);
  FCHECK(d.valid && d.drivers && d.season == 2026 && d.round == 20 && d.n == kF1Keep, "drivers after round 20");
  FCHECK_EQ(d.rows[0].code, "ANT");
  FCHECK_EQ(d.rows[0].name, "Antonelli");
  FCHECK_EQ(d.rows[0].team, "mercedes");
  FCHECK(d.rows[0].points10 == 3900 && d.rows[0].wins == 9 && d.rows[0].pos == 1, "Antonelli's 390");
  FCHECK_EQ(d.rows[8].name, "Lawson");
  FCHECK_EQ(d.rows[8].team, "red_bull");

  F1Standings c = standings_of("fixtures/jolpica_constructor_standings.json", false);
  FCHECK(c.valid && !c.drivers && c.n == 10, "ten teams kept");
  FCHECK_EQ(c.rows[0].name, "Mercedes");
  FCHECK_EQ(c.rows[0].code, "MER");
  FCHECK(c.rows[0].points10 == 6520 && c.rows[0].wins == 12, "Mercedes' 652");
  FCHECK_EQ(c.rows[4].name, "Racing Bulls");
  FCHECK_EQ(c.rows[6].name, "Haas");

  F1Frame f = f1_standings_frame(d, 128);
  FCHECK_EQ(f.title, "Drivers");
  FCHECK_EQ(f.status, "Round 20");
  FCHECK_EQ(f.rows[0].name, "Antonelli");
  FCHECK_EQ(f.rows[0].value, "9W");
  FCHECK_EQ(f.rows[0].detail, "390");
  FCHECK(f.rows[0].name_color == 0xFFD000 && f.rows[0].mark == 0x00D2BE, "the leader in gold");
  FCHECK_EQ(f.rows[1].value, "3W");
  F1Frame one = f1_standings_frame(d, 64);
  FCHECK_EQ(one.status, "Drivers");
  FCHECK_EQ(one.rows[0].name, "ANT");
  F1Frame four = f1_standings_frame(d, 256);
  FCHECK_EQ(four.title, "2026 Driver standings");
  FCHECK_EQ(four.status, "After round 20");
  FCHECK_EQ(four.rows[0].value, "Mercedes");
  FCHECK_EQ(four.rows[0].detail, "390 pts");
  FCHECK(f.ticker.find("Drivers' championship after round 20 | 1 Antonelli 390, 2 Verstappen 279") == 0,
         "the ticker: %s", f.ticker.c_str());
  FCHECK(f.spans.size() == kF1Keep, "every name colored");

  F1Frame t = f1_standings_frame(c, 128);
  FCHECK_EQ(t.title, "Teams");
  FCHECK_EQ(t.rows[0].name, "Mercedes");
  FCHECK_EQ(t.rows[0].value, "12W");
  FCHECK_EQ(t.rows[4].name, "Racing Bulls");
  FCHECK_EQ(f1_standings_frame(c, 64).rows[4].name, "RB");
  FCHECK_EQ(f1_standings_frame(c, 256).rows[0].value, "12 wins");
  for (int w : kWidths) {
    check_fits(f1_standings_frame(d, w), "drivers");
    check_fits(f1_standings_frame(c, w), "constructors");
  }

  F1Standings none;
  std::string empty = R"({"MRData":{"StandingsTable":{"season":"2027","StandingsLists":[]}}})";
  FCHECK(parse_f1_standings(empty, true, none) == 0, "before the first race");
  std::string half = R"({"MRData":{"StandingsTable":{"StandingsLists":[{"season":"2026","round":"1",)"
                     R"("DriverStandings":[{"position":"1","points":"12.5","wins":"0","Driver":{"familyName":"Zhou"}}]}]}}})";
  FCHECK(parse_f1_standings(half, true, none) == 1 && none.rows[0].points10 == 125, "half points");
  FCHECK_EQ(none.rows[0].code, "ZHO");
  FCHECK_EQ(f1_points_text(none.rows[0].points10), "12.5");
}

void test_results() {
  F1Results r = results_of();
  FCHECK(r.valid && r.round == 20 && r.n == kF1Keep && r.laps == 71, "Brazil, 71 laps");
  FCHECK_EQ(r.race, "Brazilian Grand Prix");
  FCHECK_EQ(r.circuit_id, "interlagos");
  FCHECK(r.start == parse_iso8601_z("2026-11-08T17:00:00Z"), "the race's start");
  FCHECK_EQ(r.rows[0].code, "NOR");
  FCHECK_EQ(r.rows[0].time, "1:35:23.504");
  FCHECK_EQ(r.rows[9].name, "Hulkenberg");

  FCHECK_EQ(f1_result_detail(r.rows[0], r.laps, 6), "Winner");
  FCHECK_EQ(f1_result_detail(r.rows[0], r.laps, 14), "1:35:23.504");
  FCHECK_EQ(f1_result_detail(r.rows[1], r.laps, 6), "+14.60");
  FCHECK_EQ(f1_result_detail(r.rows[1], r.laps, 14), "+14.602");
  FCHECK_EQ(f1_result_detail(r.rows[8], r.laps, 6), "+1 lap");
  FCHECK_EQ(f1_result_detail(r.rows[8], r.laps, 4), "+1L");
  F1Result x;
  strcpy(x.pos_text, "R");
  FCHECK_EQ(f1_result_detail(x, 71, 6), "DNF");
  strcpy(x.pos_text, "W");
  FCHECK_EQ(f1_result_detail(x, 71, 6), "DNS");
  strcpy(x.pos_text, "D");
  FCHECK_EQ(f1_result_detail(x, 71, 6), "DSQ");
  strcpy(x.pos_text, "N");
  FCHECK_EQ(f1_result_detail(x, 71, 6), "NC");
  strcpy(x.pos_text, "12");
  x.pos = 12;
  strcpy(x.status, "+2 Laps");
  FCHECK_EQ(f1_result_detail(x, 71, 8), "+2 laps");

  F1Frame f = f1_results_frame(r, 128);
  FCHECK_EQ(f.title, "Brazilian GP");
  FCHECK_EQ(f.status, "Last race");
  FCHECK_EQ(f.rows[0].name, "Norris");
  FCHECK_EQ(f.rows[0].value, "25");
  FCHECK_EQ(f.rows[0].detail, "Winner");
  FCHECK_EQ(f.rows[1].detail, "+14.60");
  FCHECK(f.rows[0].mark == 0xFF8700, "McLaren's color");
  FCHECK_EQ(f1_results_frame(r, 64).rows[0].name, "NOR");
  F1Frame four = f1_results_frame(r, 256);
  FCHECK_EQ(four.rows[0].value, "McLaren");
  FCHECK_EQ(four.rows[0].detail, "1:35:23.504");
  FCHECK(f.ticker.find("Brazilian Grand Prix | Result | 1 Norris 1:35:23.504, 2 Verstappen +14.602") == 0, "the ticker: %s",
         f.ticker.c_str());
  FCHECK(f.ticker.find("9 Hamilton +1 lap") != std::string::npos, "a lapped car");
  for (int w : kWidths)
    check_fits(f1_results_frame(r, w), "results");
}

void test_circuit() {
  F1CircuitFacts v = circuit_of("fixtures/f1_circuit_vegas.json");
  FCHECK(v.espn_id == 4250 && v.length_m == 6116 && v.distance_m == 305775 && v.laps == 50 && v.turns == 17,
         "Las Vegas' numbers");
  FCHECK_EQ(v.name, "Las Vegas Street Circuit");
  FCHECK_EQ(v.city, "Nevada");
  FCHECK(v.has_direction && !v.clockwise, "anticlockwise");
  FCHECK_EQ(v.record, "1:33.365");
  FCHECK(v.record_year == 2025 && v.record_athlete == 4665, "Verstappen's 2025 lap");
  F1CircuitFacts i = circuit_of("fixtures/f1_circuit_interlagos.json");
  FCHECK_EQ(i.name, "Autodromo Jose Carlos Pace");
  F1CircuitFacts h = circuit_of("fixtures/f1_circuit_hanoi.json");
  FCHECK(h.record[0] == '\0' && h.record_athlete == 0 && h.record_year == 0, "no lap record");

  F1Athlete a;
  std::string json = slurp("fixtures/f1_athlete_verstappen.json");
  FCHECK(parse_f1_athlete(json, a) == 1, "athlete");
  FCHECK_EQ(a.code, "VER");
  FCHECK_EQ(a.name, "Verstappen");
  strcpy(v.record_code, a.code);
  strcpy(v.record_name, a.name);
  std::string bad = R"({"code":404})";
  FCHECK(parse_f1_circuit(bad, h) == -1 && parse_f1_athlete(bad, a) == -1, "not a circuit or an athlete");

  const CircuitInfo *lv = circuit_by_espn(4250);
  FCHECK(lv != nullptr && strcmp(lv->key, "las-vegas") == 0, "Las Vegas by ESPN's id");
  const F1Units metric;
  F1Frame f = f1_circuit_frame(lv, v, metric, 128);
  FCHECK_EQ(f.title, "Las Vegas Street Circuit");
  FCHECK(f.n == 5, "five facts beside it: %u", (unsigned) f.n);
  FCHECK_EQ(f.rows[0].name, "6.116 km");
  FCHECK_EQ(f.rows[1].name, "50 laps");
  FCHECK_EQ(f.rows[2].name, "17 turns");
  FCHECK_EQ(f.rows[3].name, "Rec 1:33.365");
  FCHECK_EQ(f.rows[4].name, "VER 2025");
  FCHECK(f.shape_n > 20 && f.shape[0].x == f.shape[f.shape_n - 1].x && f.shape[0].y == f.shape[f.shape_n - 1].y,
         "a closed outline of %u points", (unsigned) f.shape_n);
  FCHECK(f.ticker == "Las Vegas Street Circuit, Nevada, USA | 6.116 km, 50 laps, 17 turns | Runs anticlockwise | "
                     "Lap record 1:33.365, Verstappen, 2025",
         "the ticker: %s", f.ticker.c_str());
  F1Frame four = f1_circuit_frame(lv, v, F1Units{false, true}, 256);
  FCHECK(four.n == 6, "six at 256");
  FCHECK_EQ(four.rows[0].detail, "3.800 mi");
  FCHECK_EQ(four.rows[4].detail, "Verstappen 2025");
  FCHECK_EQ(four.rows[5].detail, "Anticlockwise");
  F1Frame one = f1_circuit_frame(lv, v, metric, 64);
  FCHECK(one.n == 0 && one.shape_n > 20, "the outline alone on one panel");
  FCHECK_EQ(one.status, "Las Vegas");
  // Without ESPN's read: circuits.h's facts
  F1Frame bare = f1_circuit_frame(circuit_by_jolpica("interlagos"), F1CircuitFacts{}, metric, 128);
  FCHECK_EQ(bare.title, "Jose Carlos Pace");
  FCHECK_EQ(bare.rows[0].name, "4.309 km");
  FCHECK(bare.n == 3, "length, laps and turns");
  F1Frame pace = f1_circuit_frame(circuit_by_espn(617), i, metric, 128);
  FCHECK_EQ(pace.title, "Jose Carlos Pace");
  FCHECK(pace.n == 5, "the record's year without its driver");
  FCHECK_EQ(pace.rows[3].name, "Rec 1:10.540");
  FCHECK_EQ(pace.rows[4].name, "Set in 2018");
  FCHECK_EQ(f1_circuit_frame(circuit_by_espn(617), i, metric, 256).rows[4].detail, "2018");
  // Not in circuits.h: no outline
  F1Frame hanoi = f1_circuit_frame(nullptr, h, metric, 128);
  FCHECK(hanoi.shape_n == 0 && hanoi.n == 3, "Hanoi's facts, no outline");
  for (int w : kWidths) {
    check_fits(f1_circuit_frame(lv, v, metric, w), "circuit");
    check_fits(f1_circuit_frame(circuit_by_espn(617), i, metric, w), "interlagos");
  }
}

void test_circuits_table() {
  const size_t pool = sizeof(kCircuitPoints) / 2;
  FCHECK(kCircuitCount >= 24, "the calendar's circuits");
  for (const CircuitInfo &c : kCircuits) {
    FCHECK(c.n >= 20 && c.n <= 64 && (size_t) c.at + c.n <= pool, "%s: %u points", c.key, (unsigned) c.n);
    FCHECK((c.w == 255 || c.h == 255) && c.w > 0 && c.h > 0, "%s: its box", c.key);
    for (size_t k = 0; k < c.n; k++)
      FCHECK(kCircuitPoints[2 * (c.at + k)] <= c.w && kCircuitPoints[2 * (c.at + k) + 1] <= c.h, "%s: point %zu",
             c.key, k);
    FCHECK(c.jolpica[0] != '\0' && c.espn_id != 0 && c.length_m > 3000 && c.turns > 0, "%s: its ids and facts", c.key);
    FCHECK(circuit_by_jolpica(c.jolpica) == &c && circuit_by_espn(c.espn_id) == &c, "%s: found by id", c.key);
    FCHECK(c.lat_e4 >= -900000 && c.lat_e4 <= 900000 && c.lon_e4 >= -1800000 && c.lon_e4 <= 1800000, "%s: where",
           c.key);
  }
  FCHECK_EQ(circuit_key("Marina Bay Street Circuit"), "marina bay");
  FCHECK_EQ(circuit_key("Marina Bay Circuit"), "marina bay");
  FCHECK_EQ(circuit_key("Autódromo José Carlos Pace"), "jose carlos pace");
  FCHECK_EQ(circuit_key("Circuit of the Americas"), "americas");
  FCHECK_EQ(circuit_key(""), "");
  struct Name {
    const char *name, *key;
  };
  const Name names[] = {
      {"Las Vegas Street Circuit", "las-vegas"},     {"Marina Bay Circuit", "marina-bay"},
      {"Autódromo José Carlos Pace", "interlagos"},  {"Circuit of the Americas", "austin"},
      {"Bahrain International Circuit", "bahrain"},  {"Albert Park Grand Prix Circuit", "melbourne"},
      {"Autodromo Nazionale Monza", "monza"},        {"Circuit de Spa-Francorchamps", "spa-francorchamps"},
      {"Las Vegas Strip Circuit", "las-vegas"},      {"Circuit Gilles-Villeneuve", "montreal"},
  };
  for (const Name &n : names) {
    const CircuitInfo *c = circuit_by_name(n.name);
    FCHECK(c != nullptr && strcmp(c->key, n.key) == 0, "%s is %s, not %s", n.name, n.key, c ? c->key : "none");
  }
  FCHECK(circuit_by_name("Nowhere Raceway") == nullptr && circuit_by_name("") == nullptr &&
             circuit_by_name("Circuit") == nullptr,
         "no match");
  FCHECK(circuit_by_espn(0) == nullptr && circuit_by_espn(3885) == nullptr && circuit_by_jolpica("") == nullptr,
         "no id");
  // The header's locations in every recorded weekend
  for (const char *file : {"fixtures/racing_header_f1_final.json", "fixtures/racing_header_f1_practice.json",
                           "fixtures/racing_header_f1_sprint.json", "fixtures/racing_header_f1_canceled.json",
                           "fixtures/f1_header_vegas_pre.json"}) {
    F1Live live = live_of(file);
    FCHECK(circuit_by_name(live.snap.venue) != nullptr, "%s: \"%s\" found", file, live.snap.venue.c_str());
  }
}

void test_shape() {
  const CircuitInfo *mon = circuit_by_jolpica("monaco");
  FCHECK(mon != nullptr, "Monaco");
  if (mon == nullptr)
    return;
  F1Point pts[kF1ShapeMax];
  size_t n = f1_shape(*mon, 1, 9, 60, 46, pts, kF1ShapeMax);
  int x0 = 999, x1 = -1, y0 = 999, y1 = -1;
  for (size_t i = 0; i < n; i++) {
    x0 = std::min<int>(x0, pts[i].x);
    x1 = std::max<int>(x1, pts[i].x);
    y0 = std::min<int>(y0, pts[i].y);
    y1 = std::max<int>(y1, pts[i].y);
    if (i > 0)
      FCHECK(pts[i].x != pts[i - 1].x || pts[i].y != pts[i - 1].y, "point %zu moves", i);
  }
  // Monaco is taller than wide: the height sets the scale
  FCHECK(y0 == 9 && y1 == 54 && x0 >= 1 && x1 <= 60, "inside the box: %d-%d %d-%d", x0, x1, y0, y1);
  const int want_w = mon->w * 45 / 255;
  FCHECK(x1 - x0 >= want_w - 1 && x1 - x0 <= want_w + 1, "the aspect kept: %d wide, %d expected", x1 - x0, want_w);
  FCHECK(std::abs((x0 - 1) - (60 - 1 - x1)) <= 1, "centered across");
  FCHECK(pts[0].x == pts[n - 1].x && pts[0].y == pts[n - 1].y, "closed");
  const CircuitInfo *miami = circuit_by_jolpica("miami");
  n = f1_shape(*miami, 0, 0, 100, 100, pts, kF1ShapeMax);
  y0 = 999, y1 = -1;
  for (size_t i = 0; i < n; i++) {
    y0 = std::min<int>(y0, pts[i].y);
    y1 = std::max<int>(y1, pts[i].y);
  }
  FCHECK(std::abs(y0 - (99 - y1)) <= 1, "a wide outline centered down");
  FCHECK(f1_shape(*mon, 0, 0, 1, 46, pts, kF1ShapeMax) == 0 && f1_shape(*mon, 0, 0, 60, 46, pts, 0) == 0,
         "no box, no room");
  FCHECK(f1_shape(*mon, 0, 0, 60, 46, pts, 5) == 5, "cut at the cap");
}

void test_geom() {
  for (F1Layout l : {F1Layout::TABLE, F1Layout::LIST, F1Layout::SHAPE}) {
    for (int w : kWidths) {
      const F1Geom g = f1_geom(l, w);
      const F1Col cols[] = {g.pos, g.mark, g.name, g.value, g.detail};
      int last = -1;
      for (const F1Col &c : cols) {
        if (c.w == 0)
          continue;
        FCHECK(c.x >= 0 && c.x + c.w <= w && c.chars * 5 <= c.w, "layout %d at %d: column %d+%d", (int) l, w, c.x,
               c.w);
        FCHECK(c.x > last, "layout %d at %d: columns in order", (int) l, w);
        last = c.x + c.w - 1;
      }
      FCHECK(g.row_y0 + g.row_h * g.rows <= g.ticker_y && g.ticker_y + 8 <= 64, "rows above the ticker");
      if (l == F1Layout::SHAPE) {
        FCHECK(g.shape_w > 0 && g.shape_x >= 0 && g.shape_x + g.shape_w <= w && g.shape_y >= 8 &&
                   g.shape_y + g.shape_h <= g.ticker_y,
               "the outline's box at %d", w);
        FCHECK(g.name.w == 0 || g.name.x >= g.shape_x + g.shape_w, "facts beside the outline at %d", w);
      }
      FCHECK(g.status.chars == (w < 128 ? w / 5 : (w - 2) / 5), "the title row's characters at %d", w);
    }
  }
  // The 128 layout centered on three panels
  FCHECK(f1_geom(F1Layout::TABLE, 192).pos.x == f1_geom(F1Layout::TABLE, 128).pos.x + 32, "centered on 192");
  FCHECK(f1_geom(F1Layout::TABLE, 64).name.chars == 3 && f1_geom(F1Layout::TABLE, 128).name.chars == 12 &&
             f1_geom(F1Layout::TABLE, 256).name.chars == 16,
         "codes, then names");
}

void test_next() {
  F1Schedule s = race_of("fixtures/jolpica_next.json");
  const int64_t now = 1795003200;  // Wed Nov 18 12:00Z
  F1Frame f = f1_next_frame(s, now, utc, 128);
  FCHECK_EQ(f.title, "Las Vegas GP");
  FCHECK_EQ(f.status, "1d 12h");
  FCHECK(f.n == 5, "the weekend's five");
  FCHECK_EQ(f.rows[0].name, "FP1");
  FCHECK_EQ(f.rows[0].detail, "Fri 12:30a");
  FCHECK_EQ(f.rows[3].name, "Qual");
  FCHECK_EQ(f.rows[4].detail, "Sun 4:00a");
  FCHECK(f.rows[0].name_color == 0xFFD000 && f.rows[1].name_color == 0xFFFFFF, "the next one in gold");
  FCHECK(f.ticker.find("Las Vegas Grand Prix | Las Vegas Strip Street Circuit, Las Vegas, USA | Practice 1 Fri 12:30a") ==
             0,
         "the ticker: %s", f.ticker.c_str());
  FCHECK(f.ticker.find("Practice 1 in 1 day 12 hours") != std::string::npos, "the countdown in words");
  F1Frame four = f1_next_frame(s, now, utc, 256);
  FCHECK_EQ(four.rows[0].name, "Practice 1");
  FCHECK_EQ(four.rows[0].detail, "Fri Nov 20 12:30a");
  F1Frame one = f1_next_frame(s, now, utc, 64);
  FCHECK_EQ(one.status, "1d 12h");
  FCHECK_EQ(one.rows[1].detail, "Fr 4:00a");
  // Mid-weekend: past sessions dim, the next counts down
  const int64_t sat = 1795233600 - 1800;
  F1Frame w = f1_next_frame(s, sat, utc, 128);
  FCHECK_EQ(w.status, "30:00");
  FCHECK(w.status_color == 0xFFA000 && w.rows[0].name_color == 0x606060 && w.rows[3].name_color == 0xFFD000,
         "half an hour to qualifying");
  F1Frame race = f1_next_frame(s, 1795320000 + 600, utc, 128);
  FCHECK_EQ(race.status, "Race on");
  // A sprint weekend's short labels
  F1Schedule sp = race_of("fixtures/jolpica_race_sprint.json");
  F1Frame spf = f1_next_frame(sp, sp.sessions[0].start - kRaceSeasonGap / 45, utc, 64);
  FCHECK_EQ(spf.rows[1].name, "SQ");
  FCHECK_EQ(spf.rows[2].name, "Spr");
  for (int width : kWidths) {
    check_fits(f1_next_frame(s, now, utc, width), "next");
    check_fits(f1_next_frame(sp, now, utc, width), "sprint next");
  }
}

void test_weather() {
  F1Weather v = weather_of("fixtures/openmeteo_vegas.json");
  FCHECK(v.temp_c10 == 161 && v.wind_kmh10 == 97 && v.code == 0 && v.rain_pct == 0 && v.precip_mm10 == 0,
         "Las Vegas, clear");
  FCHECK(v.at == parse_iso8601_z("2026-11-12T03:45"), "its time");
  F1Weather m = weather_of("fixtures/openmeteo_monaco.json");
  FCHECK(m.code == 61 && m.rain_pct == 100 && m.precip_mm10 == 2 && m.temp_c10 == 166, "Monaco, raining");
  F1Weather bad;
  std::string err = R"({"error":true,"reason":"Latitude must be in range of -90 to 90"})";
  FCHECK(parse_f1_weather(err, bad) == -1 && !bad.valid, "an error answer");
  std::string junk = "not json";
  FCHECK(parse_f1_weather(junk, bad) == -1, "not JSON");

  const F1Units metric, us{true, true};
  FCHECK_EQ(f1_temp_text(161, metric), "16\xC2\xB0" "C");
  FCHECK_EQ(f1_temp_text(161, us), "61\xC2\xB0" "F");
  FCHECK_EQ(f1_temp_text(166, us), "62\xC2\xB0" "F");
  FCHECK_EQ(f1_temp_text(-25, metric), "-3\xC2\xB0" "C");
  FCHECK_EQ(f1_temp_text(-400, us), "-40\xC2\xB0" "F");
  FCHECK_EQ(f1_wind_text(97, metric), "10 km/h");
  FCHECK_EQ(f1_wind_text(97, us), "6 mph");
  FCHECK_EQ(f1_length_text(6116, metric), "6.116 km");
  FCHECK_EQ(f1_length_text(6116, us), "3.800 mi");
  for (int code = 0; code < 256; code++) {
    FCHECK(f1_weather_word((uint8_t) code).size() <= 7, "code %d's word", code);
    FCHECK(f1_weather_word((uint8_t) code).empty() == f1_weather_long((uint8_t) code).empty(), "code %d both", code);
  }
  FCHECK_EQ(f1_weather_word(0), "Clear");
  FCHECK_EQ(f1_weather_word(61), "Rain");
  FCHECK_EQ(f1_weather_long(65), "Heavy rain");
  FCHECK_EQ(f1_weather_word(95), "Storm");
  FCHECK_EQ(f1_weather_word(4), "");
  FCHECK(f1_weather_color(61) == 0x40A0FF && f1_weather_color(95) == 0xFFA000, "wet in blue, storms amber");

  F1Frame f = f1_weather_frame(v, "Las Vegas", metric, 128);
  FCHECK_EQ(f.title, "Las Vegas");
  FCHECK_EQ(f.status, "Clear");
  FCHECK_EQ(f.rows[0].name, "Air temp");
  FCHECK_EQ(f.rows[0].detail, "16\xC2\xB0" "C");
  FCHECK_EQ(f.rows[1].detail, "0%");
  FCHECK_EQ(f.rows[2].detail, "10 km/h");
  FCHECK_EQ(f.rows[3].detail, "Clear sky");
  FCHECK(f.n == 4, "no rain row when dry");
  FCHECK_EQ(f.ticker, "Weather at Las Vegas | Clear sky, 16\xC2\xB0" "C, wind 10 km/h, 0% chance of rain in the next 3 hours");
  F1Frame one = f1_weather_frame(v, "Las Vegas", metric, 64);
  FCHECK_EQ(one.status, "16\xC2\xB0" "C Clear");
  FCHECK_EQ(one.rows[0].name, "Air");
  FCHECK_EQ(one.rows[3].detail, "Clear");
  F1Frame wet = f1_weather_frame(m, "Monaco", us, 128);
  FCHECK(wet.n == 5 && wet.status_color == 0x40A0FF, "rain now");
  FCHECK_EQ(wet.rows[4].detail, "0.01 in");
  FCHECK_EQ(f1_weather_frame(m, "Monaco", metric, 128).rows[4].detail, "0.2 mm");
  FCHECK_EQ(wet.rows[1].detail, "100%");
  F1Frame none = f1_weather_frame(F1Weather{}, "Monaco", metric, 128);
  FCHECK(none.n == 0 && none.title == "Monaco", "no reading yet");
  for (int w : kWidths) {
    check_fits(f1_weather_frame(v, "Las Vegas Strip Street Circuit", us, w), "weather");
    check_fits(f1_weather_frame(m, "Monaco", metric, w), "wet weather");
  }
}

void test_text() {
  FCHECK_EQ(f1_countdown(0, 7), "Now");
  FCHECK_EQ(f1_countdown(-5, 7), "Now");
  FCHECK_EQ(f1_countdown(59, 7), "0:59");
  FCHECK_EQ(f1_countdown(3599, 7), "59:59");
  FCHECK_EQ(f1_countdown(3599, 3), "59m");
  FCHECK_EQ(f1_countdown(3600, 7), "1h 0m");
  FCHECK_EQ(f1_countdown(2 * 86400 + 4 * 3600 + 59, 7), "2d 4h");
  FCHECK_EQ(f1_countdown(2 * 86400 + 4 * 3600, 3), "2d");
  FCHECK_EQ(f1_countdown(12 * 86400 + 23 * 3600, 7), "12d 23h");
  FCHECK_EQ(f1_countdown_long(2 * 86400 + 4 * 3600), "in 2 days 4 hours");
  FCHECK_EQ(f1_countdown_long(86400), "in 1 day");
  FCHECK_EQ(f1_countdown_long(3600 + 60), "in 1 hour 1 minute");
  FCHECK_EQ(f1_countdown_long(30), "in 1 minute");
  FCHECK_EQ(f1_countdown_long(0), "now");

  const struct tm wed = utc(1795003200), thu = utc(1795105800), fri = utc(1795134600), later = utc(1795883400);
  FCHECK_EQ(f1_session_when(thu, wed, 16), "Tomorrow 4:30p");
  FCHECK_EQ(f1_session_when(thu, wed, 12), "Thu 4:30p");
  FCHECK_EQ(f1_session_when(thu, wed, 8), "Th 4:30p");
  FCHECK_EQ(f1_session_when(thu, wed, 5), "4:30p");
  FCHECK_EQ(f1_session_when(thu, thu, 12), "Today 4:30p");
  FCHECK_EQ(f1_session_when(fri, wed, 24), "Fri Nov 20 12:30a");
  FCHECK_EQ(f1_session_when(later, wed, 12), "Nov 28 4:30p");
  FCHECK_EQ(f1_session_when(later, wed, 10), "Sat 4:30p");
  struct tm bad {};
  bad.tm_mon = -1;
  FCHECK_EQ(f1_session_when(bad, wed, 12), "12:00a");

  FCHECK_EQ(f1_points_text(3900), "390");
  FCHECK_EQ(f1_points_text(125), "12.5");
  FCHECK_EQ(f1_points_text(-5), "-0.5");
  FCHECK_EQ(f1_points_text(0), "0");
  FCHECK_EQ(f1_fit("16\xC2\xB0" "C", 3), "16\xC2\xB0");
  FCHECK_EQ(f1_fit("abc", 0), "");
  FCHECK_EQ(f1_code("de Vries"), "DEV");
  FCHECK_EQ(f1_code("H\xC3\xBClkenberg"), "HUL");
  FCHECK_EQ(f1_code(""), "");
  FCHECK(f1_team("Red Bull") == f1_team("red_bull") && f1_team("RED_BULL") != nullptr, "by either name");
  FCHECK(f1_team("Racing Bulls") == f1_team("rb") && f1_team("sauber") == nullptr, "Racing Bulls, no Sauber");
  FCHECK(f1_team_color("haas") == 0xC0C0C0 && f1_team_color("nobody") == 0x808080, "lifted, or gray");
}

void test_timing() {
  F1Schedule s = race_of("fixtures/jolpica_next.json");
  const int64_t fp1 = 1795134600, race = 1795320000;
  FCHECK(!f1_race_weekend(s, fp1 - 2 * 86400) && f1_race_weekend(s, fp1 - 3600) && f1_race_weekend(s, race + 3600) &&
             !f1_race_weekend(s, race + 4 * 3600),
         "the weekend's span");
  const uint32_t min = 60 * 1000;
  FCHECK(f1_poll_ms(F1Read::WEATHER, s, fp1 - 3 * 86400) == 180 * min, "weather off the weekend");
  FCHECK(f1_poll_ms(F1Read::WEATHER, s, fp1) == 15 * min, "weather on the weekend");
  FCHECK(f1_poll_ms(F1Read::DRIVERS, s, fp1 - 3 * 86400) == 360 * min, "standings between weekends");
  FCHECK(f1_poll_ms(F1Read::CONSTRUCTORS, s, fp1) == 60 * min, "standings on the weekend");
  FCHECK(f1_poll_ms(F1Read::LAST_RESULTS, s, race + 2 * 3600) == 15 * min, "results soon after the flag");
  FCHECK(f1_poll_ms(F1Read::LAST_RESULTS, s, race + 9 * 3600) == 360 * min, "then back to slow");
  FCHECK(f1_poll_ms(F1Read::NEXT, s, fp1) == 360 * min && f1_poll_ms(F1Read::NEXT, s, race + 60) == 30 * min,
         "the next race moves on after the race");
  FCHECK(f1_poll_ms(F1Read::CIRCUIT, s, fp1) == 24 * 60 * min && f1_poll_ms(F1Read::DRIVERS, F1Schedule{}, 0) == 360 * min,
         "circuits daily, standings every 6 hours with no schedule");
  F1Live live = live_of("fixtures/racing_header_f1_live.json");
  FCHECK(f1_header_poll_ms(live, live.snap.race.session_start + 600) == 30 * 1000, "a race every 30 seconds");

  F1Have h;
  F1Screen out[8];
  FCHECK(f1_rotation(h, out, 8) == 0, "nothing yet");
  h.next = h.circuit = h.weather = h.drivers = h.constructors = h.last_race = true;
  FCHECK(f1_rotation(h, out, 8) == 6 && out[0] == F1Screen::NEXT && out[5] == F1Screen::LAST_RACE, "between weekends");
  h.live = true;
  FCHECK(f1_rotation(h, out, 8) == 7 && out[0] == F1Screen::LIVE, "a session's order first between sessions");
  h.on_track = true;
  FCHECK(f1_rotation(h, out, 8) == 1 && out[0] == F1Screen::LIVE, "on track, the order alone");
  h.on_track = false;
  FCHECK(f1_rotation(h, out, 3) == 3, "the cap");
  FCHECK(f1_frame_ms(F1Screen::LIVE) == 15000 && f1_frame_ms(F1Screen::WEATHER) == 8000 &&
             f1_frame_ms(F1Screen::DRIVERS) == 10000,
         "how long each stays");
}

void test_not_documents() {
  for (const char *doc : {"", "[]", "{}", "null", "{\"MRData\":3}", "{\"sports\":[]}"}) {
    std::string in = doc;
    F1Live l;
    F1Schedule s;
    F1Standings st;
    F1Results r;
    F1CircuitFacts c;
    F1Athlete a;
    F1Weather w;
    FCHECK(parse_f1_live(in, l) <= 0 && parse_f1_race(in, s) == -1 && parse_f1_standings(in, true, st) == -1 &&
               parse_f1_results(in, r) == -1 && parse_f1_circuit(in, c) == -1 && parse_f1_athlete(in, a) == -1 &&
               parse_f1_weather(in, w) == -1,
           "\"%s\" is none of them", doc);
    FCHECK(!s.valid && !st.valid && !r.valid && !c.valid && !a.valid && !w.valid, "\"%s\" leaves them", doc);
  }
}

}

int run_f1_tests(int &total_checks) {
  test_urls();
  test_live();
  test_race();
  test_standings();
  test_results();
  test_circuit();
  test_circuits_table();
  test_shape();
  test_geom();
  test_next();
  test_weather();
  test_text();
  test_timing();
  test_not_documents();
  total_checks += checks;
  return failures;
}
