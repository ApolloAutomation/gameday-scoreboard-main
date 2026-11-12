// Display picker tests: choices, areas, frames, a stand-in display.
#include <ArduinoJson.h>

#include <cstdio>
#include <string>
#include <vector>

#include "apps.h"
#include "board_layout.h"
#include "health.h"
#include "set_request.h"
#include "split.h"

using namespace espn;
using esphome::gameday::rows_geom;
using esphome::gameday::rows_y;
using esphome::gameday::RowsGeom;
using esphome::gameday::rows_cells;
using esphome::gameday::RowsCells;

namespace {

int checks = 0;
int failures = 0;

#define ACHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

void test_displays() {
  ACHECK(std::string(display_key(Display::GAMEDAY)) == "gameday");
  ACHECK(std::string(display_key(Display::F1)) == "f1");
  ACHECK(std::string(display_key(Display::MARKETS)) == "markets");
  ACHECK(std::string(display_key(Display::FLIGHTS)) == "flights");
  ACHECK(std::string(display_name(Display::MARKETS)) == "Stocks & Crypto");
  ACHECK(std::string(display_name(Display::FLIGHTS)) == "Flight Tracker");
  // Saved bytes: a display the firmware lacks shows Game Day.
  ACHECK(display_has(kGameDayOnly, 0) && !display_has(kGameDayOnly, 1) && !display_has(0xFF, 4));
  ACHECK(!display_has(0xFF, -1));
  ACHECK(display_effective(0, kGameDayOnly) == Display::GAMEDAY);
  ACHECK(display_effective(2, kGameDayOnly) == Display::GAMEDAY);
  ACHECK(display_effective(2, 0b101) == Display::MARKETS);
  ACHECK(display_effective(200, 0xFF) == Display::GAMEDAY);
  // A host of its own each, after Game Day's three.
  ACHECK(display_source(Display::GAMEDAY) == Source::ESPN);
  ACHECK(display_source(Display::F1) == Source::F1);
  ACHECK(display_source(Display::MARKETS) == Source::MARKETS);
  ACHECK(display_source(Display::FLIGHTS) == Source::FLIGHTS);
  ACHECK((size_t) Source::FLIGHTS + 1 == kSourceCount && kGameSourceCount == 3);
}

void test_main_view() {
  ACHECK(main_view(Display::GAMEDAY, false, true, false) == MainView::GAMEDAY);
  ACHECK(main_view(Display::F1, false, false, false) == MainView::APP);
  // Night hours show the clock, unless the display is busy.
  ACHECK(main_view(Display::F1, false, true, false) == MainView::CLOCK);
  ACHECK(main_view(Display::F1, false, true, true) == MainView::APP);
  // The demo plays on the board, whatever the choice.
  ACHECK(main_view(Display::MARKETS, true, false, false) == MainView::GAMEDAY);
  const uint8_t off = (uint8_t) IdleShow::OFF;
  ACHECK(app_main_dim(off, true, true, false, false));
  ACHECK(!app_main_dim(off, true, true, true, false));   // busy stays lit
  ACHECK(!app_main_dim(off, true, true, false, true));   // so does a busy area
  ACHECK(!app_main_dim(off, true, false, false, false)); // never dark offline
  ACHECK(!app_main_dim(off, false, true, false, false));
  ACHECK(!app_main_dim((uint8_t) IdleShow::CLOCK, true, true, false, false));
}

void test_split_values() {
  // Appended: the old values keep their numbers.
  ACHECK((int) SplitShow::CLOCK == 4 && (int) SplitShow::GAMEDAY == 5 && (int) SplitShow::FLIGHTS == 8);
  Display d = Display::F1;
  ACHECK(!split_display(SplitShow::CLOCK, d) && d == Display::F1);
  ACHECK(split_display(SplitShow::GAMEDAY, d) && d == Display::GAMEDAY);
  ACHECK(split_display(SplitShow::MARKETS, d) && d == Display::MARKETS);
  ACHECK(split_of_display(Display::FLIGHTS) == SplitShow::FLIGHTS);
  ACHECK(!area_app(SplitShow::GAMEDAY, d) && area_app(SplitShow::F1, d) && d == Display::F1);
  for (int s = 0; s <= 5; s++)
    ACHECK(split_allowed(s, kGameDayOnly));
  ACHECK(!split_allowed(6, kGameDayOnly) && !split_allowed(9, 0xFF) && !split_allowed(-1, 0xFF));
  ACHECK(split_allowed(7, 0b101) && !split_allowed(6, 0b101));
  SetOp op;
  ACHECK(parse_set("display", "3", op, 16) && op.kind == SetKind::MAIN_DISPLAY && op.value == 3);
  ACHECK(!parse_set("display", "4", op, 16) && !parse_set("display", "", op, 16));
  ACHECK(!parse_set("display", "01", op, 16) && !parse_set("display", "1 ", op, 16));
  ACHECK(parse_set("split", "8", op, 16) && op.value == 8 && !parse_set("split", "9", op, 16));
  bool listed = false;
  for (const char *k : kSetKeys)
    listed = listed || std::string(k) == "display";
  ACHECK(listed);
  // Each new choice turns the area on, three wide up.
  for (int s = 5; s < kSplitCount; s++) {
    ACHECK(split_area(3, 1, (uint8_t) s).on && split_area(4, 1, (uint8_t) s).w == 128);
    ACHECK(!split_area(2, 1, (uint8_t) s).on);
    ACHECK(split_effective((uint8_t) s, SplitAt::BOTTOM) == (SplitShow) s);
  }
}

void test_area_views() {
  AreaInputs in;
  in.show = SplitShow::GAMEDAY;
  // The board's Game Day while the main area shows another.
  ACHECK(area_view(in) == AreaView::CLOCK);
  in.main_clock = true;
  ACHECK(area_view(in) == AreaView::DATE);
  in.main_clock = false;
  in.main = Display::F1;
  ACHECK(area_view(in) == AreaView::GAME);
  in.night = true;  // its own idle rules decide at night
  ACHECK(area_view(in) == AreaView::GAME);
  ACHECK(std::string(area_view_key(AreaView::GAME)) == "game" && std::string(area_view_key(AreaView::APP)) == "app");

  in = AreaInputs{};
  in.show = SplitShow::MARKETS;
  ACHECK(area_view(in) == AreaView::CLOCK);  // the firmware lacks it
  in.displays = 0b101;
  ACHECK(area_view(in) == AreaView::APP);
  in.main = Display::MARKETS;  // never on both
  ACHECK(area_view(in) == AreaView::CLOCK);
  in.main = Display::F1;
  ACHECK(area_view(in) == AreaView::APP);
  in.night = true;
  ACHECK(area_view(in) == AreaView::CLOCK);
  in.app_busy = true;
  ACHECK(area_view(in) == AreaView::APP);
  // The old choices ignore the displays.
  in = AreaInputs{};
  in.show = SplitShow::CLOCK;
  in.main = Display::F1;
  in.displays = 0xF;
  ACHECK(area_view(in) == AreaView::CLOCK);
  in.show = SplitShow::SCORES;
  in.scores = true;
  ACHECK(area_view(in) == AreaView::SCORES);
}

void test_frame_helpers() {
  // A square keeps its shape, centered in the box.
  const std::vector<float> xs = {0, 10, 10, 0, 0}, ys = {0, 0, 10, 10, 0};
  std::vector<AppPoint> sq = app_line_fit(xs, ys, AppBox{0, 8, 128, 48}, true);
  ACHECK(sq.size() == 5);
  if (sq.size() == 5) {
    ACHECK(sq[0].x == 40 && sq[0].y == 55);  // y up: the first corner low
    ACHECK(sq[1].x == 87 && sq[1].y == 55);
    ACHECK(sq[2].x == 87 && sq[2].y == 8);
    ACHECK(sq[0] == sq[4]);
  }
  // Stretched, it fills the box.
  std::vector<AppPoint> wide = app_line_fit(xs, ys, AppBox{0, 0, 64, 32}, false);
  ACHECK(wide.size() == 5 && wide[1].x == 63 && wide[2].y == 0 && wide[0].y == 31);
  // A sparkline: left to right, the high at the top.
  std::vector<AppPoint> sp = app_sparkline({1, 3, 2}, AppBox{2, 10, 61, 21});
  ACHECK(sp.size() == 3);
  if (sp.size() == 3)
    ACHECK(sp[0].x == 2 && sp[0].y == 30 && sp[1].x == 32 && sp[1].y == 10 && sp[2].x == 62 && sp[2].y == 20);
  // A flat series sits in the middle.
  std::vector<AppPoint> flat = app_sparkline({5, 5, 5}, AppBox{0, 0, 64, 21});
  ACHECK(flat.size() == 3 && flat[0].y == 10 && flat[2].y == 10);
  ACHECK(app_sparkline({5}, AppBox{0, 0, 64, 21}).empty());
  ACHECK(app_line_fit(xs, ys, AppBox{0, 0, 1, 40}, true).empty());
  // Too many points: the first and last kept, evenly between.
  std::vector<float> many_x, many_y;
  for (int i = 0; i < 500; i++) {
    many_x.push_back((float) i);
    many_y.push_back((float) (i % 7));
  }
  std::vector<AppPoint> thin = app_line_fit(many_x, many_y, AppBox{0, 0, 256, 64}, false);
  ACHECK(thin.size() == kAppLinePoints && thin.front().x == 0 && thin.back().x == 255);
  ACHECK(app_line_thin(thin, 1).empty() && app_line_thin(thin, 100).size() == kAppLinePoints);
  // Flattened inside the area, clipped.
  AppLine line;
  line.pts = {{-5, 3}, {70, 80}, {10, 10}};
  std::vector<int16_t> xy = app_line_xy(line, 64, 64);
  ACHECK(xy == std::vector<int16_t>({0, 3, 63, 63, 10, 10}));
  line.pts.resize(1);
  ACHECK(app_line_xy(line, 64, 64).empty());
  // Rows cut to six; a lone line counts too.
  AppFrame f;
  ACHECK(!app_frame_board(f));
  for (int i = 0; i < 9; i++) {
    f.rows.pos.push_back(std::to_string(i + 1));
    f.rows.name.push_back("N");
    f.rows.value_color.push_back(0xFFFFFF);
  }
  app_frame_trim(f);
  ACHECK(f.rows.pos.size() == 6 && f.rows.name.size() == 6 && f.rows.value_color.size() == 6);
  ACHECK(app_frame_board(f));
  AppFrame g;
  g.line.pts = {{0, 0}, {1, 1}};
  ACHECK(app_frame_board(g));
}

void test_rows_geom() {
  // The same places gameday-live.yaml gives the board's rows.
  const RowsGeom one = rows_geom(64);
  ACHECK(!one.wide && one.status.w == 64 && one.pos.x == 0 && one.name.x == 12 && one.name.w == 35);
  ACHECK(one.value.x == 49 && one.value.w == 15 && one.name_no_value == 52);
  const RowsGeom two = rows_geom(128);
  ACHECK(two.wide && two.title.x == 2 && two.title.w == 124 && two.status.w == 126);
  ACHECK(two.pos.x == 2 && two.name.x == 16 && two.name.w == 55 && two.value.x == 74 && two.detail.x == 94);
  const RowsGeom four = rows_geom(256);
  ACHECK(four.pos.x == 66 && four.detail.x == 158 && four.title.w == 252);
  ACHECK(rows_y(0) == 8 && rows_y(5) == 48);
  // Golf and racing keep the leaderboard's own cells.
  const RowsCells golf = rows_cells(64, true, {});
  ACHECK(!golf.own && golf.cell[0].x == 0 && golf.cell[1].w == 35 && golf.cell[2].x == 49 && golf.cell[2].w == 15);
  ACHECK(golf.cell[3].w == 0);
  const RowsCells race = rows_cells(64, false, {});
  ACHECK(race.cell[1].w == 52 && race.cell[2].w == 0 && race.cell[3].w == 0);
  const RowsCells wide = rows_cells(128, false, {});
  ACHECK(wide.cell[0].x == 2 && wide.cell[1].w == 55 && wide.cell[2].w == 15 && wide.cell[3].x == 94);
  ACHECK(wide.cell[3].w == 30 && rows_cells(256, true, {}).cell[3].x == 158);
}

void test_units() {
  // US and UK: miles and mph; US alone: Fahrenheit.
  ACHECK(tz_miles(0) && tz_fahrenheit(0) && tz_miles(6) && tz_fahrenheit(6));
  ACHECK(tz_miles(10) && !tz_fahrenheit(10));
  ACHECK(!tz_miles(7) && !tz_fahrenheit(7) && !tz_miles(13) && !tz_miles(-1) && !tz_fahrenheit(200));
}

void test_hosts() {
  ACHECK(url_host("https://API.Example.com:8443/v1?x=1") == "api.example.com");
  ACHECK(url_host("http://example.com") == "example.com");
  ACHECK(url_host("https://example.com/path") == "example.com");
  ACHECK(host_is("api.example.com", "example.com") && host_is("example.com", "example.com"));
  ACHECK(!host_is("badexample.com", "example.com") && !host_is("example.com", "") && !host_is("com", "example.com"));
  const AppHost hosts[] = {{"example.com", Source::F1}, {"data.example.org", Source::FLIGHTS}};
  ACHECK(url_source("https://api.example.com/x", hosts, 2) == Source::F1);
  ACHECK(url_source("https://data.example.org/x", hosts, 2) == Source::FLIGHTS);
  ACHECK(url_source("https://example.org/x", hosts, 2) == Source::ESPN);
  ACHECK(url_source("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard", hosts, 2) ==
         Source::ESPN);
  ACHECK(url_source("https://statsapi.mlb.com/api/v1/schedule", hosts, 2) == Source::MLB_STATS);
  ACHECK(url_source("https://lscluster.hockeytech.com/feed/", hosts, 2) == Source::HOCKEYTECH);
  ACHECK(url_source("https://api.example.com/x", hosts, 0) == Source::ESPN);
}

void test_timing() {
  ACHECK(app_due(5000, 0, 60000));
  ACHECK(!app_due(5000, 1, 60000) && app_due(60001, 1, 60000));
  ACHECK(app_due(0x00000100u, 0xFFFFFF00u, 0x100));  // across the wrap
  AppTimes t;
  ACHECK(t.expect(Display::F1) == AppTimes::kFirstMs);
  t.took(Display::F1, 1200, true);
  ACHECK(t.expect(Display::F1) == 2200);
  t.took(Display::F1, 900, false);  // a quick failure says nothing
  ACHECK(t.expect(Display::F1) == 2200);
  t.took(Display::F1, 9000, false);  // a slow one does
  ACHECK(t.expect(Display::F1) == 10000);
  t.skip(Display::F1);
  t.cycle();
  ACHECK(t.ms[(int) Display::F1] == 9000 - 9000 / 8);
  t.started(Display::F1);
  t.cycle();
  ACHECK(t.ms[(int) Display::F1] == 9000 - 9000 / 8);
  t.took(Display::MARKETS, 999999, true);
  ACHECK(t.ms[(int) Display::MARKETS] == AppTimes::kMaxMs);
}

// A stand-in display: a number from a small JSON body.
class TestApp final : public App {
 public:
  struct Prefs {
    uint8_t unit;
  };
  Prefs saved{0};
  int value{0}, pending{0};
  bool got{false}, on_screen{false};
  Display id() const override { return Display::MARKETS; }
  const char *const *hosts() const override {
    static const char *const kHosts[] = {"example.com", nullptr};
    return kHosts;
  }
  void *prefs() override { return &this->saved; }
  size_t prefs_size() const override { return sizeof(Prefs); }
  const char *const *set_keys() const override {
    static const char *const kKeys[] = {"testunit", nullptr};
    return kKeys;
  }
  AppSet set(const std::string &key, const std::string &v) override {
    if (key != "testunit" || v.size() != 1 || v[0] < '0' || v[0] > '1')
      return AppSet::REFUSED;
    const uint8_t u = (uint8_t) (v[0] - '0');
    if (u == this->saved.unit)
      return AppSet::SAME;
    this->saved.unit = u;
    return AppSet::CHANGED;
  }
  void state(JsonObject out, int64_t /*now*/) const override { out["v"] = this->value; }
  void shown(bool on) override { this->on_screen = on; }
  uint32_t poll_ms(int64_t) const override { return this->got ? 60000 : 0; }
  bool request(int64_t, AppRequest &out) override {
    out.url = "https://api.example.com/v1/price?unit=" + std::to_string(this->saved.unit);
    out.headers.push_back({"User-Agent", "gameday-scoreboard"});
    return true;
  }
  bool parse(AppStream &in, int64_t) override {
    JsonDocument doc;
    if (deserializeJson(doc, in))
      return false;
    this->pending = doc["price"] | -1;
    return this->pending >= 0;
  }
  void apply(bool ok, int64_t) override {
    if (ok) {
      this->value = this->pending;
      this->got = true;
    }
  }
  void frame(int w, int h, int64_t, AppFrame &out) const override {
    out.rows.title = "PRICE";
    out.rows.pos.push_back("1");
    out.rows.name.push_back("TEST");
    out.rows.value.push_back(std::to_string(this->value));
    out.rows.value_color.push_back(0x2ED573);
    out.rows.name_color.push_back(0xFFFFFF);
    out.ticker = "TEST " + std::to_string(this->value);
    out.spans.push_back({"TEST", 0x2ED573});
    out.line.pts = app_sparkline({1, 2, 3}, AppBox{0, 8, (int16_t) w, (int16_t) (h - 16)});
  }
};

void test_app_slots() {
  AppSlots slots;
  ACHECK(slots.mask() == kGameDayOnly && slots.get(Display::F1) == nullptr);
  TestApp a;
  ACHECK(app_register(slots, &a));
  ACHECK(!app_register(slots, &a));  // its slot is taken
  ACHECK(!app_register(slots, nullptr));
  ACHECK(slots.mask() == 0b101 && slots.get(Display::MARKETS) == &a);
  AppHost hosts[4];
  const size_t n = app_hosts(slots, hosts, 4);
  ACHECK(n == 1 && hosts[0].source == Source::MARKETS);
  ACHECK(url_source("https://api.example.com/v1", hosts, n) == Source::MARKETS);
  ACHECK(app_for_key(slots, "testunit") == Display::MARKETS && app_for_key(slots, "split") == Display::GAMEDAY);
  ACHECK(app_hosts(slots, hosts, 0) == 0);

  // Through the interface, as the firmware runs it.
  ACHECK(a.set("testunit", "1") == AppSet::CHANGED && a.set("testunit", "1") == AppSet::SAME);
  ACHECK(a.set("testunit", "7") == AppSet::REFUSED && a.saved.unit == 1);
  ACHECK(app_due(1000, 0, a.poll_ms(0)));
  AppRequest req;
  ACHECK(a.request(0, req) && req.url == "https://api.example.com/v1/price?unit=1" && req.body.empty());
  AppTextStream body("{\"price\": 42, \"extra\": [1, 2, 3]}");
  ACHECK(a.parse(body, 0));
  ACHECK(a.value == 0);  // nothing shows before apply
  a.apply(true, 0);
  ACHECK(a.value == 42 && a.poll_ms(0) == 60000);
  AppTextStream bad("not json");
  ACHECK(!a.parse(bad, 0));
  a.apply(false, 0);
  ACHECK(a.value == 42);
  AppFrame f;
  a.frame(128, 64, 0, f);
  app_frame_trim(f);
  ACHECK(app_frame_board(f) && f.rows.value[0] == "42" && f.line.pts.size() == 3);
  ACHECK(app_line_xy(f.line, 128, 64).size() == 6);
  JsonDocument doc;
  a.state(doc[display_key(a.id())].to<JsonObject>(), 0);
  ACHECK(doc["markets"]["v"].as<int>() == 42);
}

void test_health_apps() {
  std::string out = "{";
  health_app_json(out, "f1", 200, -1);  // none read yet
  health_app_json(out, nullptr, 200, 7);
  ACHECK(out == "{");
  health_app_json(out, "f1", 200, 7);
  health_app_json(out, "markets", 999, 4000000000LL);
  out += "}";
  JsonDocument doc;
  out[1] = ' ';  // the leading comma
  ACHECK(!deserializeJson(doc, out));
  ACHECK(doc["f1"].as<int>() == 200 && doc["f1_ago"].as<int>() == 7);
  ACHECK(doc["markets"].as<int>() == 999 && doc["markets_ago"].as<int64_t>() == 4000000000LL);
  ACHECK(doc["flights"].isNull());
}

}  // namespace

int run_app_tests(int &total_checks) {
  test_displays();
  test_main_view();
  test_split_values();
  test_area_views();
  test_frame_helpers();
  test_rows_geom();
  test_units();
  test_hosts();
  test_timing();
  test_app_slots();
  test_health_apps();
  total_checks += checks;
  return failures;
}
