// The F1 display's data, text and frames; no ESPHome.
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "circuits.h"
#include "espn_parse.h"
#include "panel_text.h"

namespace espn {

static const size_t kF1Keep = 10;
static const size_t kF1Rows = 6;
static const size_t kF1Sessions = 6;
static const size_t kF1ShapeMax = 72;
static const char *const kF1UserAgent = "GameDayScoreboard/1.0";

// ESPN's per-car extras beside its RaceRow.
struct F1Car {
  char code[4]{};
  char team[16]{};  // ESPN's manufacturer: "Red Bull"
  uint32_t rgb{0};  // ESPN's teamColor, 0 none
  uint16_t laps{0};
  uint8_t pits{0};
  uint8_t grid{0};  // startOrder, 0 none
  bool has_pits{false};
};
static_assert(std::is_trivially_copyable<F1Car>::value, "cars copy as a block");

// cars[i] belongs to snap.race.rows[i].
struct F1Live {
  GameSnapshot snap;
  F1Car cars[kRaceRows];
};

struct F1Session {
  RaceKind kind{RaceKind::RACE};
  uint8_t number{0};
  int64_t start{0};
  bool timed{true};  // false: a date without a time
};

struct F1Schedule {
  bool valid{false};
  uint16_t season{0};
  uint8_t round{0};
  char race[40]{};
  char circuit_id[24]{};  // Jolpica's circuit id: "vegas"
  char circuit[48]{};
  char locality[24]{}, country[24]{};
  int32_t lat_e4{0}, lon_e4{0};
  bool has_pos{false};
  uint8_t n{0};
  F1Session sessions[kF1Sessions];  // by start, the race last
};

struct F1Standing {
  uint8_t pos{0};
  char code[4]{};
  char name[20]{};  // family name, or the constructor's
  char team[16]{};  // constructorId, a driver's latest
  int32_t points10{0};
  uint8_t wins{0};
};
static_assert(std::is_trivially_copyable<F1Standing>::value, "standings copy as a block");

struct F1Standings {
  bool valid{false};
  bool drivers{true};
  uint16_t season{0};
  uint8_t round{0};
  uint8_t n{0};
  F1Standing rows[kF1Keep];
};

struct F1Result {
  uint8_t pos{0};
  char pos_text[4]{};  // "1", or "R", "W", "D", "N"
  char code[4]{};
  char name[20]{};
  char team[16]{};
  char time[12]{};    // "1:35:23.504", "+14.602", "" none
  char status[16]{};  // "Finished", "Lapped", "+1 Lap", "Retired"
  uint16_t laps{0};
  uint8_t grid{0};
  int16_t points10{0};
  bool fastest{false};
};
static_assert(std::is_trivially_copyable<F1Result>::value, "results copy as a block");

struct F1Results {
  bool valid{false};
  uint16_t season{0};
  uint8_t round{0};
  char race[40]{};
  char circuit_id[24]{};
  int64_t start{0};
  uint16_t laps{0};  // the winner's lap count
  uint8_t n{0};
  F1Result rows[kF1Keep];
};

struct F1CircuitFacts {
  bool valid{false};
  uint32_t espn_id{0};
  char name[48]{};
  char city[24]{}, country[24]{};
  uint16_t length_m{0};
  uint32_t distance_m{0};
  uint8_t laps{0}, turns{0};
  bool clockwise{true}, has_direction{false};
  char record[12]{};  // fastest lap "1:33.365", "" none
  uint16_t record_year{0};
  uint32_t record_athlete{0};  // ESPN athlete id for f1_athlete_url
  char record_code[4]{};       // filled from the athlete read
  char record_name[20]{};
};

struct F1Athlete {
  bool valid{false};
  char code[4]{};
  char name[20]{};
};

struct F1Weather {
  bool valid{false};
  int64_t at{0};
  int16_t temp_c10{0};
  int16_t wind_kmh10{0};
  uint16_t precip_mm10{0};
  uint8_t code{0};      // WMO weather code
  int8_t rain_pct{-1};  // most of the next hours', -1 none
};

enum class F1Read : uint8_t { HEADER, NEXT, DRIVERS, CONSTRUCTORS, LAST_RESULTS, CIRCUIT, ATHLETE, WEATHER };

// NEXT, DRIVERS, CONSTRUCTORS, LAST_RESULTS; "" otherwise.
std::string f1_jolpica_url(F1Read read);
std::string f1_round_url(uint8_t round);
std::string f1_circuit_url(uint32_t espn_id);
std::string f1_athlete_url(uint32_t espn_id);
// Always metric; the text functions convert.
std::string f1_weather_url(int32_t lat_e4, int32_t lon_e4);
// A day before the first session to race end.
bool f1_race_weekend(const F1Schedule &s, int64_t now);
uint32_t f1_poll_ms(F1Read read, const F1Schedule &s, int64_t now);
uint32_t f1_header_poll_ms(const F1Live &live, int64_t now);

// "Marina Bay Street Circuit" is "marina bay".
std::string circuit_key(const std::string &name);
const CircuitInfo *circuit_by_espn(uint32_t espn_id);
const CircuitInfo *circuit_by_jolpica(const std::string &id);
// Exact key first, else the longest key it contains.
const CircuitInfo *circuit_by_name(const std::string &name);
const CircuitInfo *circuit_for(const F1Schedule &s);

struct F1Point {
  int16_t x, y;
};
// Aspect kept, centered; a closed outline repeats its first point.
size_t f1_shape(const CircuitInfo &c, int16_t x, int16_t y, int16_t w, int16_t h, F1Point *out, size_t cap);

struct F1Team {
  const char *id;    // Jolpica's constructor id
  const char *espn;  // ESPN's manufacturer name
  const char *name;
  const char *code;
  uint32_t rgb;  // ESPN's team color
};
const F1Team *f1_team(const std::string &id_or_manufacturer);
// Lifted for the panel; gray when unknown.
uint32_t f1_team_color(const std::string &id_or_manufacturer);
// "de Vries" is "DEV".
std::string f1_code(const std::string &last_name);
// Standings codes by family name.
void f1_live_codes(F1Live &live, const F1Standings &drivers);

struct F1Units {
  bool fahrenheit{false};
  bool mph{false};  // also lengths in miles
};
std::string f1_temp_text(int16_t c10, const F1Units &u);
std::string f1_wind_text(int16_t kmh10, const F1Units &u);
std::string f1_length_text(uint32_t meters, const F1Units &u);
// Short words fit 7 characters; "" when unknown.
std::string f1_weather_word(uint8_t code);
std::string f1_weather_long(uint8_t code);
uint32_t f1_weather_color(uint8_t code);
// "2d 4h", "4h 12m", "12:05", "Now".
std::string f1_countdown(int64_t seconds, size_t max);
// "in 2 days 4 hours", "now".
std::string f1_countdown_long(int64_t seconds);
// "Today 4:30p", "Thu 4:30p", "Th 4:30p".
std::string f1_session_when(const struct tm &start, const struct tm &now, size_t max);
std::string f1_points_text(int32_t points10);
// "+14.60", "+1L", "DNF", "DNS", "DSQ", "NC", "Winner".
std::string f1_result_detail(const F1Result &r, uint16_t leader_laps, size_t max);
// Cut by glyphs, so UTF-8 stays whole.
std::string f1_fit(const std::string &s, size_t max);

enum class F1Screen : uint8_t { LIVE, NEXT, CIRCUIT, WEATHER, DRIVERS, CONSTRUCTORS, LAST_RACE };
// TABLE columns, LIST label and value, SHAPE outline beside LIST.
enum class F1Layout : uint8_t { TABLE, LIST, SHAPE };
enum class F1Align : uint8_t { LEFT, RIGHT, CENTER };

// chars: spleen_8 characters, 5 px each; w 0 hidden.
struct F1Col {
  int16_t x{0}, w{0};
  uint8_t chars{0};
  F1Align align{F1Align::LEFT};
};

// Title row at y 0, rows below it.
struct F1Geom {
  int16_t w{64};
  F1Col title, status;
  F1Col pos, mark, name, value, detail;
  int16_t row_y0{8}, row_h{8};
  uint8_t rows{0};
  int16_t shape_x{0}, shape_y{0}, shape_w{0}, shape_h{0};
  int16_t ticker_y{56};
};
// 64, 128 and 256 wide; other widths centered.
F1Geom f1_geom(F1Layout layout, int width);

struct F1Row {
  char pos[4]{};
  char name[32]{};
  char value[20]{};
  char detail[28]{};
  uint32_t mark{0};  // team color mark, 0 none
  uint32_t pos_color{0xA0A0A0}, name_color{0xFFFFFF}, value_color{0xA0A0A0}, detail_color{0xA0A0A0};
};
static_assert(std::is_trivially_copyable<F1Row>::value, "frame rows copy as a block");

struct F1Frame {
  F1Screen screen{F1Screen::LIVE};
  F1Layout layout{F1Layout::TABLE};
  int16_t width{64};
  std::string title, status;
  uint32_t title_color{0xFFFFFF}, status_color{0xFFFFFF};
  uint8_t n{0};
  F1Row rows[kF1Rows];
  // Plain text: panel_text, then ticker_markup with spans.
  std::string ticker;
  std::vector<esphome::gameday::TickerSpan> spans;
  uint8_t shape_n{0};
  F1Point shape[kF1ShapeMax];
  uint32_t shape_color{0xFFFFFF};
};

bool f1_live_has_board(const F1Live &live);
// when: the next session's racing_when, for the ticker.
F1Frame f1_live_frame(const F1Live &live, int width, const std::string &when);
// local[i] is sessions[i]'s local start.
F1Frame f1_next_frame_tm(const F1Schedule &s, int64_t now, const struct tm *local, const struct tm &now_local,
                         int width);
// Either may be missing; circuit null means no outline.
F1Frame f1_circuit_frame(const CircuitInfo *circuit, const F1CircuitFacts &facts, const F1Units &u, int width);
F1Frame f1_weather_frame(const F1Weather &w, const std::string &place, const F1Units &u, int width);
F1Frame f1_standings_frame(const F1Standings &s, int width);
F1Frame f1_results_frame(const F1Results &r, int width);

struct F1Have {
  bool live{false};      // set from f1_live_has_board
  bool on_track{false};  // racing_on_track: LIVE alone
  bool next{false}, circuit{false}, weather{false}, drivers{false}, constructors{false}, last_race{false};
};
// The screens to rotate, in order.
size_t f1_rotation(const F1Have &h, F1Screen *out, size_t cap);
uint32_t f1_frame_ms(F1Screen s);

template<typename ToLocal>
F1Frame f1_next_frame(const F1Schedule &s, int64_t now, ToLocal to_local, int width) {
  struct tm local[kF1Sessions];
  for (size_t i = 0; i < s.n && i < kF1Sessions; i++)
    local[i] = to_local(s.sessions[i].start);
  return f1_next_frame_tm(s, now, local, to_local(now), width);
}

namespace detail {
void fill_f1_live_filter(JsonDocument &f);
int f1_live_from_doc(JsonObjectConst root, F1Live &out);
void fill_f1_race_filter(JsonDocument &f);
int f1_race_from_doc(JsonObjectConst root, F1Schedule &out);
void fill_f1_standings_filter(JsonDocument &f, bool drivers);
int f1_standings_from_doc(JsonObjectConst root, bool drivers, F1Standings &out);
void fill_f1_results_filter(JsonDocument &f);
int f1_results_from_doc(JsonObjectConst root, F1Results &out);
void fill_f1_circuit_filter(JsonDocument &f);
int f1_circuit_from_doc(JsonObjectConst root, F1CircuitFacts &out);
void fill_f1_athlete_filter(JsonDocument &f);
int f1_athlete_from_doc(JsonObjectConst root, F1Athlete &out);
void fill_f1_weather_filter(JsonDocument &f);
int f1_weather_from_doc(JsonObjectConst root, F1Weather &out);

template<typename TInput, typename Fill, typename From> int f1_parse(TInput &input, Fill fill, From from) {
  JsonDocument filter(json_allocator());
  fill(filter);
  JsonDocument doc(json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  return from(doc.as<JsonObjectConst>());
}
}

// 1 read, 0 none listed, -1 not that document.
template<typename TInput> int parse_f1_live(TInput &input, F1Live &out) {
  return detail::f1_parse(input, detail::fill_f1_live_filter,
                          [&out](JsonObjectConst r) { return detail::f1_live_from_doc(r, out); });
}
template<typename TInput> int parse_f1_race(TInput &input, F1Schedule &out) {
  return detail::f1_parse(input, detail::fill_f1_race_filter,
                          [&out](JsonObjectConst r) { return detail::f1_race_from_doc(r, out); });
}
template<typename TInput> int parse_f1_standings(TInput &input, bool drivers, F1Standings &out) {
  return detail::f1_parse(
      input, [drivers](JsonDocument &f) { detail::fill_f1_standings_filter(f, drivers); },
      [&out, drivers](JsonObjectConst r) { return detail::f1_standings_from_doc(r, drivers, out); });
}
template<typename TInput> int parse_f1_results(TInput &input, F1Results &out) {
  return detail::f1_parse(input, detail::fill_f1_results_filter,
                          [&out](JsonObjectConst r) { return detail::f1_results_from_doc(r, out); });
}
template<typename TInput> int parse_f1_circuit(TInput &input, F1CircuitFacts &out) {
  return detail::f1_parse(input, detail::fill_f1_circuit_filter,
                          [&out](JsonObjectConst r) { return detail::f1_circuit_from_doc(r, out); });
}
template<typename TInput> int parse_f1_athlete(TInput &input, F1Athlete &out) {
  return detail::f1_parse(input, detail::fill_f1_athlete_filter,
                          [&out](JsonObjectConst r) { return detail::f1_athlete_from_doc(r, out); });
}
template<typename TInput> int parse_f1_weather(TInput &input, F1Weather &out) {
  return detail::f1_parse(input, detail::fill_f1_weather_filter,
                          [&out](JsonObjectConst r) { return detail::f1_weather_from_doc(r, out); });
}

}
