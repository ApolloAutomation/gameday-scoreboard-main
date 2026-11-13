// Field view: the model from real documents, picture and frames
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ArduinoJson.h"
#include "field.h"
#include "idle.h"
#include "set_request.h"

using namespace espn;
using esphome::gameday::BoardFont;
using esphome::gameday::font_rows;
using esphome::gameday::kBoardRows;

namespace {

int checks = 0;
int failures = 0;

#define FCHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

GameSnapshot event(const char *file, uint32_t team) {
  GameSnapshot g;
  FCHECK(parse_event_str(slurp(std::string("fixtures/") + file), Sport::FOOTBALL, team, g));
  return g;
}

std::string day_event(int i) {
  JsonDocument doc;
  deserializeJson(doc, slurp("fixtures/scoreboard_ncaa_live.json"), DeserializationOption::NestingLimit(40));
  std::string out;
  serializeJson(doc["events"][i], out);
  return out;
}

GameSnapshot day(int i, uint32_t team) {
  GameSnapshot g;
  FCHECK(parse_event_str(day_event(i), Sport::FOOTBALL, team, g));
  return g;
}

void test_parse() {
  // BUF, away, at its own 6 (ESPN's 94)
  const GameSnapshot buf = event("event_nfl_live.json", 2);
  const Football &f = buf.fb;
  FCHECK(f.yard_line == 94 && f.down == 2 && f.distance == 5);
  FCHECK(f.play_start == 99 && f.play_end == 94 && f.drive_start == 99);
  FCHECK(f.drive_side == 1 && !f.drive_over);
  FCHECK(f.drive_plays == 1 && f.drive_yards == 5 && f.drive_secs == 0);
  FCHECK(f.home_win == 31);
  FCHECK(f.periods == 2 && f.line[0][0] == 3 && f.line[0][1] == 0 && f.line[1][0] == 0);
  FCHECK(f.leaders.find("J. Allen 5/12, 43 YDS, 1 INT") == 0);
  FCHECK(f.leaders.find(" | J. Cook III 13 CAR, 23 YDS") != std::string::npos);
  // The other side reads the same document
  const GameSnapshot min = event("event_nfl_live.json", 16);
  FCHECK(min.fb.drive_side == 2 && min.fb.line[1][0] == 3);
  // A drive's words
  Football d;
  espn::detail::drive_numbers("7 plays, 38 yards, 1:36", d);
  FCHECK(d.drive_plays == 7 && d.drive_yards == 38 && d.drive_secs == 96);
  d = Football();
  espn::detail::drive_numbers("1 play, -1 yard, 0:14", d);
  FCHECK(d.drive_plays == 1 && d.drive_yards == -1 && d.drive_secs == 14);
  d = Football();
  espn::detail::drive_numbers("", d);
  FCHECK(d.drive_plays == 0 && d.drive_secs == -1);
  d = Football();
  espn::detail::drive_numbers("no numbers here", d);
  FCHECK(d.drive_plays == 0);
  // A day's scoreboard read and the watcher's carry no field
  GameSnapshot sb;
  std::string day_json = slurp("fixtures/scoreboard_ncaa_live.json");
  FCHECK(parse_scoreboard(day_json, std::string("401858433"), 68, sb));
  FCHECK(sb.fb.yard_line < 0 && !field_ready(sb));
  // Other sports never read it
  GameSnapshot nba;
  FCHECK(parse_event_str(slurp("fixtures/event_nba_live.json"), Sport::BASKETBALL, 0, nba) || true);
  FCHECK(nba.fb.yard_line < 0);
}

void test_model() {
  // BUF on the left, attacking right from its own 6
  FieldModel m = field_model(event("event_nfl_live.json", 2));
  FCHECK(m.on && m.spot && !m.brk && m.side == 0 && m.ball == 6 && m.gain == 11 && !m.goal && !m.red);
  FCHECK(m.drive == 1 && m.play0 == 1 && m.play1 == 6 && m.win == 69);
  FCHECK(std::string(m.abbr[0]) == "BUF" && m.score[0] == 3 && m.score[1] == 0);
  // MIN's board: BUF on the right, attacking left
  m = field_model(event("event_nfl_live.json", 16));
  FCHECK(m.spot && m.side == 1 && m.ball == 94 && m.gain == 89 && m.drive == 99 && m.win == 31);
  // FIU away at its own 31, 3rd and 4
  m = field_model(event("event_ncaa_live.json", 2229));
  FCHECK(m.spot && m.side == 0 && m.ball == 31 && m.gain == 35 && m.drive == 25 && m.win == 4);
  FCHECK(m.play0 == 25 && m.play1 == 31);
  // DUKE home, 3rd and goal at TULN 10: red zone
  m = field_model(day(6, 150));
  FCHECK(m.spot && m.side == 0 && m.ball == 90 && m.goal && m.gain == -1 && m.red && m.drive == 52);
  // TULN's board: the same play, mirrored
  m = field_model(day(6, 2655));
  FCHECK(m.spot && m.side == 1 && m.ball == 10 && m.goal && m.red && m.drive == 48);
  // A red zone's edge: the 20 counts, the 21 doesn't
  GameSnapshot g = event("event_ncaa_live.json", 2229);
  g.fb.yard_line = 20;
  FCHECK(field_model(g).red && field_model(g).ball == 80);
  g.fb.yard_line = 21;
  FCHECK(!field_model(g).red);
  // A gain past the goal line is goal to go
  g.fb.yard_line = 8;
  g.fb.distance = 10;
  FCHECK(field_model(g).goal && field_model(g).gain == -1);
  // A touchdown: no down, no spot
  m = field_model(day(11, 2026));
  FCHECK(m.on && !m.spot && m.gain == -1);
  // Halftime: the line score
  m = field_model(day(7, 2429));
  FCHECK(m.on && m.brk && !m.spot && m.periods == 2 && m.line[0][0] == 7 && m.line[0][1] == 7 && m.line[1][1] == 0);
  // A drive ESPN starts at 0 has no start
  m = field_model(day(2, 276));
  FCHECK(m.spot && m.side == 0 && m.ball == 3 && m.drive == -1 && m.play0 == 1 && m.play1 == 3);
  // An interception: new side has it, old drive over
  m = field_model(day(5, 2132));
  FCHECK(m.spot && m.side == 0 && m.ball == 62 && m.drive == -1);
  // The other side's play: no arrow, no drive
  g = event("event_nfl_live.json", 2);
  g.fb.drive_side = 2;
  m = field_model(g);
  FCHECK(m.spot && m.drive == -1 && m.play0 == -1);
  // Possession changes sides with the ball
  g = event("event_nfl_live.json", 2);
  g.possession = 2;
  m = field_model(g);
  FCHECK(m.spot && m.side == 1 && m.gain == 1 && m.red && m.drive == -1);
  // Missing data hides the spot, or the field
  g = event("event_nfl_live.json", 2);
  g.possession = 0;
  FCHECK(field_model(g).on && !field_model(g).spot);
  g = event("event_nfl_live.json", 2);
  g.fb.down = 0;
  FCHECK(!field_model(g).spot);
  g.fb.yard_line = -1;
  FCHECK(!field_ready(g) && !field_model(g).on);
  g = event("event_nfl_live.json", 2);
  g.state = GameState::POST;
  FCHECK(!field_model(g).on);
  g = event("event_nfl_pre.json", 1);
  FCHECK(!field_ready(g));
  // Overtime: the fifth period on, in the OT column
  g = event("event_ncaa_live.json", 2229);
  g.period = 5;
  g.fb.periods = 6;
  g.fb.line[0][4] = 14;
  m = field_model(g);
  FCHECK(m.spot && m.periods == 6 && m.line[0][4] == 14);
  FCHECK(field_down(m, false) == "3rd & 4");
}

void test_words() {
  const GameSnapshot buf = event("event_nfl_live.json", 2);
  FieldModel m = field_model(buf);
  FCHECK(field_down(m, false) == "2nd & 5" && field_down(m, true) == "2&5");
  FCHECK(field_spot(buf, false) == "BUF 6" && field_spot(buf, true) == "BUF6");
  FCHECK(field_drive(buf.fb, false) == "1 play, 5 yards, 0:00" && field_drive(buf.fb, true) == "1P 5Y 0:00");
  FCHECK(field_win(buf) == "BUF 69%");
  const GameSnapshot duke = day(6, 150);
  m = field_model(duke);
  FCHECK(field_down(m, false) == "3rd & Goal" && field_down(m, true) == "3&G");
  FCHECK(field_spot(duke, false) == "TULN 10" && field_win(duke) == "DUKE 84%");
  FCHECK(field_drive(duke.fb, false) == "7 plays, 38 yards, 1:36");
  GameSnapshot mid = buf;
  mid.fb.yard_line = 50;
  FCHECK(field_spot(mid, false) == "50");
  // The ticker: last play, leaders, the drive, the chance
  const std::string t = field_ticker(duke, true);
  FCHECK(t.find("W.Eget") != std::string::npos);
  FCHECK(t.find(" | Drive: 7 plays, 38 yards, 1:36 | DUKE 84% to win") != std::string::npos);
  FCHECK(field_ticker(duke, false).find("W.Eget pass") == std::string::npos);
  // No drive after a turnover, nor at a break
  FCHECK(field_ticker(day(5, 2132), true).find("Drive") == std::string::npos);
  FCHECK(field_ticker(day(7, 2429), true).find("Drive") == std::string::npos);
}

// Labels and picture stay on the board, above the ticker
void check_frame(const SportFrame &fr, int w, bool large, const char *what) {
  const int ty = kBoardRows - (large ? 12 : 8);
  bool ok = fr.pic.w == w && fr.pic.h == ty && fr.pic.x == 0 && fr.pic.y == 0;
  ok = ok && fr.pic.fx >= 0 && fr.pic.fx + fr.pic.fw <= w && fr.pic.fy >= 0 && fr.pic.fy + fr.pic.fh <= ty;
  ok = ok && fr.pic.fh >= 16;
  for (const AppText &t : fr.texts) {
    const bool in = t.x >= 0 && t.x + t.w <= w && t.y >= 0 && t.y + font_rows(t.font) <= ty;
    if (!in)
      printf("  %s %d: \"%s\" at %d,%d w %d\n", what, w, t.text.c_str(), t.x, t.y, t.w);
    ok = ok && in;
    // Big digits only where spleen_24 has them
    if (t.font == BoardFont::S24 || t.font == BoardFont::S32)
      for (char c : t.text)
        ok = ok && ((c >= '0' && c <= '9') || c == ' ' || c == ':');
  }
  FCHECK(ok);
}

void test_frames() {
  std::vector<GameSnapshot> games = {event("event_nfl_live.json", 2), event("event_nfl_live.json", 16),
                                     event("event_ncaa_live.json", 2229), day(6, 150), day(11, 2026),
                                     day(7, 2429), day(2, 276), field_sample("", 0, "", 0)};
  for (const GameSnapshot &g : games) {
    const std::string clock = board_clock_text(g, false, 0);
    for (int large = 0; large < 2; large++) {
      for (int w : {64, 128, 192, 256}) {
        SportFrame fr;
        sport_frame(g, clock, w, large, fr);
        check_frame(fr, w, large, "board");
        FCHECK(fr.texts.size() <= kAppTexts);
      }
      for (int w : {64, 128}) {
        SportFrame fr;
        detail_frame(g, clock, w, large, fr);
        check_frame(fr, w, large, "area");
        FCHECK(fr.pic.win_y < 0 || fr.pic.win_y + 2 <= fr.pic.h);
      }
    }
  }
  // The down row says it, red in the red zone
  SportFrame fr;
  const GameSnapshot duke = day(6, 150);
  sport_frame(duke, board_clock_text(duke, false, 0), 128, false, fr);
  bool red = false;
  for (const AppText &t : fr.texts)
    red = red || (t.text.find("3rd & Goal") == 0 && t.color == 0xFF3030);
  FCHECK(red);
  // Possession's gold bar under the side with the ball
  FCHECK(fr.pic.poss_y >= 0 && fr.pic.poss_x < 64);
  // Halftime shows each period's points, no bar
  const GameSnapshot half = day(7, 2429);
  sport_frame(half, board_clock_text(half, false, 0), 256, false, fr);
  int periods = 0;
  for (const AppText &t : fr.texts)
    periods += t.text == "1" || t.text == "2" || t.text == "T";
  FCHECK(periods >= 3 && fr.pic.poss_y < 0);
  // Two same frames compare same; a moved ball doesn't
  SportFrame a, b;
  const GameSnapshot buf = event("event_nfl_live.json", 2);
  sport_frame(buf, "8:44 2nd", 128, false, a);
  sport_frame(buf, "8:44 2nd", 128, false, b);
  FCHECK(field_same(a.pic, b.pic));
  GameSnapshot moved = buf;
  moved.fb.yard_line = 80;
  sport_frame(moved, "8:44 2nd", 128, false, b);
  FCHECK(!field_same(a.pic, b.pic));
}

uint32_t at(const std::vector<uint16_t> &buf, const FieldPic &p, int x, int y) {
  const uint16_t c = buf[(size_t) y * p.w + x];
  return ((uint32_t) (c >> 11) << 19) | ((uint32_t) ((c >> 5) & 63) << 10) | ((uint32_t) (c & 31) << 3);
}

std::vector<uint16_t> paint(const FieldPic &pic) {
  std::vector<uint16_t> buf((size_t) pic.w * pic.h, 0x1234);
  Px565 p;
  p.px = buf.data();
  p.w = p.stride = pic.w;
  p.h = pic.h;
  p.clip = PxBox{0, 0, pic.w, pic.h};
  field_paint(p, pic);
  return buf;
}

void test_picture() {
  const GameSnapshot fiu = event("event_ncaa_live.json", 2229);
  for (int w : {64, 128, 192, 256}) {
    SportFrame fr;
    sport_frame(fiu, "0:33 4th", w, false, fr);
    const FieldPic &p = fr.pic;
    const std::vector<uint16_t> buf = paint(p);
    // Every pixel painted; black outside the field
    bool all = true;
    for (uint16_t c : buf)
      all = all && c != 0x1234;
    FCHECK(all);
    const int top = p.fy + 1, mid = p.fy + p.fh / 2;
    // Scrimmage blue, the line to gain yellow, the ball brown
    FCHECK(at(buf, p, field_x(p, 31), top) == 0x3070F8);
    FCHECK(at(buf, p, field_x(p, 35), top) == 0xF8D000);
    FCHECK((at(buf, p, field_x(p, 31), mid - 1) >> 16) == 0xB0);
    // End zones in each side's color, field green between
    FCHECK(at(buf, p, p.fx + 1, mid) != at(buf, p, p.fx + p.fw - 2, mid));
    FCHECK(((at(buf, p, field_x(p, 60), mid + 1) >> 8) & 255) > (at(buf, p, field_x(p, 60), mid + 1) >> 16));
    // The yards map in order, inside the goal lines
    FCHECK(field_x(p, 0) > p.fx && field_x(p, 100) < p.fx + p.fw - 1 && field_x(p, 50) > field_x(p, 49));
  }
  // The red zone is tinted, the rest isn't
  const GameSnapshot duke = day(6, 150);
  SportFrame fr;
  sport_frame(duke, "", 128, false, fr);
  const std::vector<uint16_t> buf = paint(fr.pic);
  const int y = fr.pic.fy + fr.pic.fh / 2 + 3;
  const uint32_t in = at(buf, fr.pic, field_x(fr.pic, 85), y), out = at(buf, fr.pic, field_x(fr.pic, 45), y);
  FCHECK((in >> 16) > ((in >> 8) & 255) && ((out >> 8) & 255) > (out >> 16));
  // Same inputs, same bytes
  FCHECK(buf == paint(fr.pic));
  // No field: all black
  SportFrame none;
  GameSnapshot g = duke;
  g.state = GameState::POST;
  sport_frame(g, "", 128, false, none);
  bool black = true;
  for (uint16_t c : paint(none.pic))
    black = black && c == 0;
  FCHECK(black);
}

void test_sample() {
  const GameSnapshot g = field_sample("BUF", 0x00338D, "MIA", 0x008E97);
  const FieldModel m = field_model(g);
  FCHECK(field_ready(g) && m.spot && m.side == 0 && m.ball == 66 && m.gain == 73 && m.drive == 25 && m.win == 64);
  FCHECK(std::string(m.abbr[1]) == "MIA");
  // No teams on the board: made-up ones, never twins
  const GameSnapshot none = field_sample("", 0, "", 0);
  FCHECK(none.team_abbr == "HOME" && none.opp_abbr == "AWAY");
  FCHECK(field_sample("BUF", 0, "BUF", 0).opp_abbr == "AWAY");
}

}  // namespace

int run_field_tests(int &total_checks) {
  test_parse();
  test_model();
  test_words();
  test_frames();
  test_picture();
  test_sample();
  total_checks += checks;
  return failures;
}
