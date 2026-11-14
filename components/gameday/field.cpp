#include "field.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "board_layout.h"
#include "scene.h"

namespace espn {

namespace detail {

void fill_field_filter(JsonObject ev) {
  JsonObject comp = ev["competitions"][0];
  JsonObject sit = comp["situation"];
  sit["yardLine"] = true;
  sit["down"] = true;
  sit["distance"] = true;
  JsonObject lp = sit["lastPlay"];
  lp["start"]["yardLine"] = true;
  lp["end"]["yardLine"] = true;
  lp["team"]["id"] = true;
  lp["drive"]["description"] = true;
  lp["drive"]["start"]["yardLine"] = true;
  lp["drive"]["result"] = true;
  lp["probability"]["homeWinPercentage"] = true;
  JsonObject l = filter_first(filter_first(comp, "leaders"), "leaders");
  l["displayValue"] = true;
  l["athlete"]["shortName"] = true;
  filter_first(filter_first(comp, "competitors"), "linescores")["value"] = true;
}

namespace {

int8_t yard_of(JsonVariantConst v) {
  if (!v.is<float>())
    return -1;
  const long y = lround(v.as<float>());
  return y >= 0 && y <= 100 ? (int8_t) y : -1;
}

int int_of(JsonVariantConst v, int lo, int hi, int none) {
  if (!v.is<float>())
    return none;
  const long n = lround(v.as<float>());
  return n < lo ? lo : n > hi ? hi : (int) n;
}

}  // namespace

void drive_numbers(const std::string &desc, Football &f) {
  int n[4] = {0, 0, 0, 0}, k = 0;
  for (size_t i = 0; i < desc.size() && k < 4;) {
    const bool neg = desc[i] == '-' && i + 1 < desc.size() && isdigit((unsigned char) desc[i + 1]);
    if (!neg && !isdigit((unsigned char) desc[i])) {
      i++;
      continue;
    }
    if (neg)
      i++;
    long v = 0;
    while (i < desc.size() && isdigit((unsigned char) desc[i]) && v < 10000)
      v = v * 10 + (desc[i++] - '0');
    n[k++] = (int) (neg ? -v : v);
  }
  if (k < 2 || desc.find("play") == std::string::npos || desc.find("yard") == std::string::npos)
    return;
  f.drive_plays = (uint8_t) std::max(0, std::min(n[0], 99));
  f.drive_yards = (int16_t) std::max(-99, std::min(n[1], 199));
  if (k == 4 && n[2] >= 0 && n[2] < 60 && n[3] >= 0 && n[3] < 60)
    f.drive_secs = (int16_t) (n[2] * 60 + n[3]);
}

void field_from_event(JsonObjectConst ev, GameSnapshot &s) {
  Football &f = s.fb;
  f = Football();
  JsonObjectConst comp = ev["competitions"][0];
  JsonObjectConst sit = comp["situation"];
  f.yard_line = yard_of(sit["yardLine"]);
  f.down = (int8_t) int_of(sit["down"], -1, 4, -1);
  f.distance = (int8_t) int_of(sit["distance"], -1, 99, -1);
  JsonObjectConst lp = sit["lastPlay"];
  f.play_start = yard_of(lp["start"]["yardLine"]);
  f.play_end = yard_of(lp["end"]["yardLine"]);
  const uint32_t by = id_of(lp["team"]["id"]);
  f.drive_side = (int8_t) (by == 0 ? 0 : by == s.team_id ? 1 : by == s.opp_id ? 2 : 0);
  JsonObjectConst drive = lp["drive"];
  f.drive_start = yard_of(drive["start"]["yardLine"]);
  f.drive_over = !str_or_empty(drive["result"]).empty();
  drive_numbers(str_or_empty(drive["description"]), f);
  JsonVariantConst hw = lp["probability"]["homeWinPercentage"];
  if (hw.is<float>()) {
    const long p = lround(hw.as<float>() * 100.0f);
    f.home_win = (int8_t) (p < 0 ? 0 : p > 100 ? 100 : p);
  }
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    const uint32_t id = id_of(c["id"]);
    const int side = id == s.team_id ? 0 : id == s.opp_id ? 1 : -1;
    if (side < 0)
      continue;
    int i = 0;
    for (JsonObjectConst q : c["linescores"].as<JsonArrayConst>()) {
      uint8_t &cell = f.line[side][i < 4 ? i : 4];
      cell = (uint8_t) std::min(255, cell + int_of(q["value"], 0, 255, 0));
      if (i < 255)
        i++;
    }
    f.periods = (uint8_t) std::max<int>(f.periods, i);
  }
  if (s.state != GameState::IN)
    return;
  for (JsonObjectConst cat : comp["leaders"].as<JsonArrayConst>()) {
    JsonObjectConst l = cat["leaders"][0];
    const std::string who = str_or_empty(l["athlete"]["shortName"]), what = str_or_empty(l["displayValue"]);
    if (who.empty() || what.empty() || f.leaders.size() > 160)
      continue;
    if (!f.leaders.empty())
      f.leaders += " | ";
    f.leaders += who + " " + what;
  }
}

}  // namespace detail

// ---- The model -------------------------------------------------------------------

bool field_same(const FieldPic &a, const FieldPic &b) { return memcmp(&a, &b, sizeof(FieldPic)) == 0; }

int field_left(const GameSnapshot &g, int yard_line) {
  if (yard_line < 0 || yard_line > 100)
    return -1;
  return g.team_home ? yard_line : 100 - yard_line;
}

uint32_t field_color(uint32_t team) {
  // As the scoreboard lifts names, unless lost on turf
  const uint32_t lifted = esphome::gameday::lift_color(team, 0xC0);
  const Kit k = kit_colors(lifted, 0);
  return k.jersey;
}

FieldModel field_model(const GameSnapshot &g) {
  FieldModel m;
  if (!field_ready(g))
    return m;
  const Football &f = g.fb;
  m.on = 1;
  m.color[0] = field_color(parse_color(g.team_color));
  m.color[1] = field_color(parse_color(g.opp_color));
  m.score[0] = (int16_t) std::max(0, std::min(g.team_score, 999));
  m.score[1] = (int16_t) std::max(0, std::min(g.opp_score, 999));
  snprintf(m.abbr[0], sizeof m.abbr[0], "%s", g.team_abbr.c_str());
  snprintf(m.abbr[1], sizeof m.abbr[1], "%s", g.opp_abbr.c_str());
  m.periods = std::min<uint8_t>(f.periods, 9);
  memcpy(m.line, f.line, sizeof m.line);
  if (f.home_win >= 0)
    m.win = (int8_t) (g.team_home ? f.home_win : 100 - f.home_win);
  m.brk = football_break(g) ? 1 : 0;
  const int side = g.possession == 1 ? 0 : g.possession == 2 ? 1 : -1;
  const int ball = field_left(g, f.yard_line);
  if (m.brk || side < 0 || f.down < 1 || ball <= 0 || ball >= 100)
    return m;
  m.spot = 1;
  m.side = (int8_t) side;
  m.ball = (int8_t) ball;
  m.down = f.down;
  m.dist = (int8_t) std::max<int>(0, f.distance);
  const int dir = side == 0 ? 1 : -1, to_goal = side == 0 ? 100 - ball : ball;
  m.goal = g.short_down_distance.find("Goal") != std::string::npos || f.distance >= to_goal ? 1 : 0;
  if (!m.goal && f.distance > 0)
    m.gain = (int8_t) (ball + dir * f.distance);
  m.red = to_goal <= 20 ? 1 : 0;
  // The drive is its: the last play was theirs
  const bool theirs = f.drive_side == side + 1;
  if (theirs && !f.drive_over && f.drive_start > 0 && f.drive_start < 100)
    m.drive = (int8_t) field_left(g, f.drive_start);
  if (theirs && f.play_start > 0 && f.play_start < 100 && f.play_end > 0 && f.play_end < 100 &&
      f.play_start != f.play_end) {
    m.play0 = (int8_t) field_left(g, f.play_start);
    m.play1 = (int8_t) field_left(g, f.play_end);
  }
  return m;
}

// ---- The picture -----------------------------------------------------------------

namespace {

constexpr uint32_t kTurf[2] = {0x0A4A18, 0x083E14}, kRedTurf[2] = {0x521616, 0x461212};
constexpr uint32_t kYardLine = 0x4E704E, kMidLine = 0x7E9E7E, kGoal = 0xB4B4B4, kNumber = 0x587858;
constexpr uint32_t kScrimmage = 0x3070FF, kToGain = 0xFFD000, kGold = 0xFFD000, kUsed = 0x202020;
constexpr uint32_t kBallRgb = 0xB0602A, kLace = 0xF0F0F0;

int ez_of(int fw) { return std::max(3, (fw * 10 + 60) / 120); }
int span_of(const FieldPic &p) { return p.fw - 2 * ez_of(p.fw) - 1; }

void text(Px565 &p, int x, int y, const char *s, uint32_t c) { px_text(p, x, y, s, rgb565(c), 1); }

// The end zone's letters, stacked
void ez_letters(Px565 &p, int x, int w, int y, int h, const char *abbr, uint32_t c) {
  const int n = (int) strnlen(abbr, 5);
  if (n == 0 || w < 5 || h < 6 * n + 1)
    return;
  const int top = y + (h - (6 * n - 1)) / 2, left = x + (w - 3) / 2;
  for (int i = 0; i < n; i++) {
    const char one[2] = {abbr[i], 0};
    text(p, left, top + 6 * i, one, c);
  }
}

void paint_field(Px565 &p, const FieldPic &pic) {
  const FieldModel &m = pic.m;
  const int ez = ez_of(pic.fw), span = span_of(pic);
  const int gl = pic.fx + ez, gr = gl + span;
  const int top = pic.fy, bot = pic.fy + pic.fh;
  // Turf in 10-yard bands; the red zone tinted
  for (int x = gl; x <= gr; x++) {
    const int yd = (x - gl) * 100 / std::max(1, span);
    const int band = std::min(yd / 10, 9);
    const bool red = m.red && (m.side == 0 ? yd >= 80 : yd < 20);
    p.vline(x, top, pic.fh, rgb565((red ? kRedTurf : kTurf)[band & 1]));
  }
  for (int s = 0; s < 2; s++) {
    const int x = s == 0 ? pic.fx : gr + 1;
    p.fill(x, top, ez, pic.fh, rgb565(dim_rgb(m.color[s], 30)));
    ez_letters(p, x, ez, top, pic.fh, m.abbr[s], m.color[s]);
  }
  for (int k = 1; k < 10; k++)
    p.vline(field_x(pic, 10 * k), top, pic.fh, rgb565(k == 5 ? kMidLine : kYardLine));
  p.vline(gl, top, pic.fh, rgb565(kGoal));
  p.vline(gr, top, pic.fh, rgb565(kGoal));
  // Numbers straddle their lines where two fit
  if (pic.numbers && span * 10 / 100 >= 10 && pic.fh >= 16) {
    for (int k = 1; k < 10; k++) {
      char buf[3];
      snprintf(buf, sizeof buf, "%d", 10 * (k <= 5 ? k : 10 - k));
      const int x = field_x(pic, 10 * k);
      text(p, x - 3, bot - 7, buf, kNumber);
      if (pic.fh >= 30)
        text(p, x - 3, top + 2, buf, kNumber);
      p.vline(x, bot - 7, 5, rgb565(k == 5 ? kMidLine : kYardLine));
      if (pic.fh >= 30)
        p.vline(x, top + 2, 5, rgb565(k == 5 ? kMidLine : kYardLine));
    }
  }
  if (!m.spot)
    return;
  const uint32_t mine = m.color[m.side];
  const int bx = field_x(pic, m.ball), mid = top + pic.fh / 2;
  const int dir = m.side == 0 ? 1 : -1;
  if (m.drive >= 0) {
    const int dx = field_x(pic, m.drive);
    p.fill(std::min(dx, bx), bot - 2, std::abs(bx - dx) + 1, 2, rgb565(mine));
  }
  if (m.play0 >= 0) {
    const int x0 = field_x(pic, m.play0), x1 = field_x(pic, m.play1);
    // Below the top numbers where they show
    const int row = top + (pic.fh >= 30 ? 9 : std::max(1, pic.fh / 5)), d = x1 > x0 ? 1 : -1;
    p.hline(std::min(x0, x1), row, std::abs(x1 - x0) + 1, rgb565(0xB0B0B0));
    p.put(x1 - d, row - 1, rgb565(0xB0B0B0));
    p.put(x1 - d, row + 1, rgb565(0xB0B0B0));
  }
  p.vline(bx, top, pic.fh, rgb565(kScrimmage));
  if (m.gain >= 0)
    p.vline(field_x(pic, m.gain), top, pic.fh, rgb565(kToGain));
  // The ball, then its carrier's arrow ahead of it
  const bool small = pic.fw < 100;
  const int bw = small ? 4 : 5;
  const int bl = bx - bw / 2;
  p.hline(bl + 1, mid - 1, bw - 2, rgb565(kBallRgb));
  p.hline(bl, mid, bw, rgb565(kBallRgb));
  p.hline(bl + 1, mid + 1, bw - 2, rgb565(kBallRgb));
  if (!small)
    p.put(bx, mid, rgb565(kLace));
  const int ax = bx + dir * (bw / 2 + 2);
  const int arm = pic.fh >= 20 ? 2 : 1;
  for (int i = -arm; i <= arm; i++) {
    const int reach = arm - std::abs(i);
    p.put(ax + dir * reach, mid + i, rgb565(mine));
    p.put(ax + dir * (reach - 1), mid + i, rgb565(mine));
  }
}

}  // namespace

int field_x(const FieldPic &p, int yards) {
  const int span = span_of(p);
  return p.fx + ez_of(p.fw) + (yards * span + 50) / 100;
}

void field_paint(Px565 &p, const FieldPic &pic) {
  p.fill(0, 0, pic.w, pic.h, 0);
  const FieldModel &m = pic.m;
  if (!m.on)
    return;
  // At a break the frame's line score shows
  if (!(m.brk && m.periods > 0))
    paint_field(p, pic);
  if (pic.pip_y >= 0)
    for (int s = 0; s < 2; s++) {
      if (pic.to[s] < 0)
        continue;
      for (int i = 0; i < 3; i++) {
        const int x = s == 0 ? pic.pip_x[0] + 6 * i : pic.pip_x[1] - 3 - 6 * i;
        p.fill(x, pic.pip_y, 4, 2, rgb565(i < pic.to[s] ? m.color[s] : kUsed));
      }
    }
  if (pic.poss_y >= 0)
    p.fill(pic.poss_x, pic.poss_y, pic.poss_w, pic.poss_h, rgb565(kGold));
  if (pic.win_y >= 0 && m.win >= 0) {
    const int split = pic.fx + pic.fw * m.win / 100;
    p.fill(pic.fx, pic.win_y, split - pic.fx, 2, rgb565(dim_rgb(m.color[0], 80)));
    p.fill(split, pic.win_y, pic.fx + pic.fw - split, 2, rgb565(dim_rgb(m.color[1], 80)));
  }
}

// ---- Words -----------------------------------------------------------------------

std::string field_down(const FieldModel &m, bool short_form) {
  if (!m.spot || m.down < 1 || m.down > 4)
    return "";
  static const char *const kOrd[] = {"", "1st", "2nd", "3rd", "4th"};
  const std::string to_go = m.goal ? (short_form ? "G" : "Goal") : std::to_string(m.dist);
  return short_form ? std::to_string(m.down) + "&" + to_go : std::string(kOrd[m.down]) + " & " + to_go;
}

std::string field_spot(const GameSnapshot &g, bool short_form) {
  const int yl = g.fb.yard_line;
  if (yl <= 0 || yl >= 100)
    return "";
  if (yl == 50)
    return "50";
  // ESPN's 0 is the home goal line
  const std::string &home = g.team_home ? g.team_abbr : g.opp_abbr;
  const std::string &away = g.team_home ? g.opp_abbr : g.team_abbr;
  const bool home_half = yl < 50;
  return (home_half ? home : away) + (short_form ? "" : " ") + std::to_string(home_half ? yl : 100 - yl);
}

std::string field_drive(const Football &f, bool short_form) {
  if (f.drive_plays == 0)
    return "";
  char buf[48];
  char t[8] = "";
  if (f.drive_secs >= 0)
    snprintf(t, sizeof t, "%d:%02d", f.drive_secs / 60, f.drive_secs % 60);
  if (short_form)
    snprintf(buf, sizeof buf, "%dP %dY%s%s", f.drive_plays, f.drive_yards, t[0] ? " " : "", t);
  else
    snprintf(buf, sizeof buf, "%d play%s, %d yard%s%s%s", f.drive_plays, f.drive_plays == 1 ? "" : "s",
             f.drive_yards, std::abs(f.drive_yards) == 1 ? "" : "s", t[0] ? ", " : "", t);
  return buf;
}

std::string field_win(const GameSnapshot &g) {
  const int hw = g.fb.home_win;
  if (hw < 0)
    return "";
  const std::string &home = g.team_home ? g.team_abbr : g.opp_abbr;
  const std::string &away = g.team_home ? g.opp_abbr : g.team_abbr;
  return hw >= 50 ? home + " " + std::to_string(hw) + "%" : away + " " + std::to_string(100 - hw) + "%";
}

std::string field_ticker(const GameSnapshot &g, bool last_play) {
  std::string out;
  if (last_play)
    detail::add_last_play(out, g.last_play);
  detail::add_part(out, g.fb.leaders);
  const Football &f = g.fb;
  const int side = g.possession == 1 ? 1 : g.possession == 2 ? 2 : 0;
  if (side != 0 && f.drive_side == side && !f.drive_over && !football_break(g)) {
    const std::string d = field_drive(f, false);
    if (!d.empty())
      detail::add_part(out, "Drive: " + d);
  }
  const std::string w = field_win(g);
  if (!w.empty())
    detail::add_part(out, w + " to win");
  return out;
}

// ---- The frames ------------------------------------------------------------------

namespace {

using esphome::gameday::BoardFont;
using esphome::gameday::font_px;
using esphome::gameday::kBoardRows;
using esphome::gameday::lift_color;

void put(std::vector<AppText> &out, int x, int y, int w, BoardFont f, AppAlign a, uint32_t c, const std::string &s) {
  if (s.empty() || w <= 0)
    return;
  AppText t;
  t.x = (int16_t) x;
  t.y = (int16_t) y;
  t.w = (int16_t) w;
  t.font = f;
  t.align = a;
  t.color = c;
  t.text = s;
  out.push_back(std::move(t));
}

// One row (top empty), or period over time
struct ClockForm {
  std::string_view top, bottom;
};

// Whole first, then two rows, then shorter; q holds "Q4"
int clock_forms(std::string_view c, bool rows_first, char (&q)[2], ClockForm (&out)[4]) {
  constexpr size_t kNone = std::string_view::npos;
  int n = 0;
  const size_t sp = c.find(' '), last = c.rfind(' ');
  if (sp != kNone && c.find(':') < sp) {
    const std::string_view time = c.substr(0, sp), period = c.substr(sp + 1);
    if (!rows_first)
      out[n++] = {{}, c};
    out[n++] = {period, time};
    // "4th" as "Q4"
    if (period.size() == 3 && period[0] >= '1' && period[0] <= '4' && isalpha((unsigned char) period[1])) {
      q[0] = 'Q';
      q[1] = period[0];
      out[n++] = {std::string_view(q, 2), time};
    }
    out[n++] = {{}, time};
    return n;
  }
  out[n++] = {{}, c};
  // "End of 3rd": "End" over "3rd"
  if (sp != kNone)
    out[n++] = {c.substr(0, sp), c.substr(last + 1)};
  out[n++] = {{}, c == "Halftime" ? std::string_view("Half") : c.substr(last == kNone ? 0 : last + 1)};
  return n;
}

// Where a clock goes: one row, or two
struct ClockBox {
  int x{0}, w{0}, y12{-1}, y8{0};    // one row; y12 -1: S8 only
  int top_x{0}, top_w{0}, top_y{0};  // two rows; top_w 0: none
  int bot_x{0}, bot_w{0}, bot_y{0};
};

int px_of(std::string_view s, BoardFont f) { return (int) s.size() * font_px(f); }

// The first form that fits; force: the last regardless
bool place_clock(std::vector<AppText> &t, const std::string &clock, bool rows_first, const ClockBox &b, bool force) {
  char q[2];
  ClockForm forms[4];
  const int n = clock_forms(clock, rows_first, q, forms);
  for (int i = 0; i < n; i++) {
    const ClockForm &f = forms[i];
    if (f.top.empty()) {
      const bool big = b.y12 >= 0 && px_of(f.bottom, BoardFont::S12) <= b.w;
      if (big || px_of(f.bottom, BoardFont::S8) <= b.w || (force && i + 1 == n)) {
        put(t, b.x, big ? b.y12 : b.y8, b.w, big ? BoardFont::S12 : BoardFont::S8, AppAlign::CENTER, 0xFFFFFF,
            std::string(f.bottom));
        return true;
      }
    } else if (b.top_w > 0 && px_of(f.top, BoardFont::S8) <= b.top_w && px_of(f.bottom, BoardFont::S8) <= b.bot_w) {
      const bool timed = f.bottom.find(':') != std::string_view::npos;
      put(t, b.top_x, b.top_y, b.top_w, BoardFont::S8, AppAlign::CENTER, timed ? 0xB4B4B4 : 0xFFFFFF,
          std::string(f.top));
      put(t, b.bot_x, b.bot_y, b.bot_w, BoardFont::S8, AppAlign::CENTER, 0xFFFFFF, std::string(f.bottom));
      return true;
    }
  }
  return false;
}

// Down, then where, as long as fits
std::string down_row(const FieldModel &m, const GameSnapshot &g, int room_px, bool short_form) {
  const std::string d = field_down(m, short_form), at = field_spot(g, short_form), tight = field_spot(g, true);
  if (d.empty())
    return "";
  for (const std::string &s : {d + " at " + at, d + " " + at, d + " " + tight})
    if (!at.empty() && px_of(s, BoardFont::S8) <= room_px)
      return s;
  return d;
}

// The spot hidden: ESPN's play type, where it fits
std::string hidden_row(const GameSnapshot &g, const FieldModel &m, int room_px) {
  if (m.brk || m.spot || px_of(g.play_type, BoardFont::S8) > room_px)
    return "";
  return g.play_type;
}

// Each period's points; the total last
void line_score(std::vector<AppText> &t, const FieldModel &m, int x, int y, int w, int h) {
  const int ot = m.periods > 4 ? 1 : 0, cols = 5 + ot;
  const bool names = w >= 96;
  const int name_w = names ? 22 : 0, cw = std::min(15, (w - name_w) / cols);
  const int x0 = x + (w - name_w - cols * cw) / 2, y0 = y + std::max(0, (h - 24) / 2);
  static const char *const kCols[] = {"1", "2", "3", "4", "OT", "T"};
  for (int s = -1; s < 2; s++) {
    const int ry = y0 + 8 * (s + 1);
    if (s >= 0 && names)
      put(t, x0, ry, name_w, BoardFont::S8, AppAlign::LEFT, m.color[s], m.abbr[s]);
    int col = 0;
    for (int i = 0; i < 6; i++) {
      if (i == 4 && !ot)
        continue;
      std::string v = kCols[i];
      if (s >= 0)
        v = i == 5 ? std::to_string(m.score[s]) : i < m.periods || i == 4 ? std::to_string(m.line[s][i]) : "-";
      const uint32_t c = s < 0 ? 0x707070 : names && i < 5 ? 0xB4B4B4 : names ? 0xFFFFFF : m.color[s];
      put(t, x0 + name_w + col * cw, ry, cw, BoardFont::S8, AppAlign::CENTER, c, v);
      col++;
    }
  }
}

uint32_t abbr_rgb(const GameSnapshot &g, int side) {
  return field_color(parse_color(side == 0 ? g.team_color : g.opp_color));
}

int clamp_to(int v) { return v < -1 ? -1 : v > 3 ? 3 : v; }

}  // namespace

void sport_frame(const GameSnapshot &g, const std::string &clock, int w, bool large, SportFrame &out) {
  out.texts.clear();
  out.pic = FieldPic();
  FieldPic &p = out.pic;
  p.m = field_model(g);
  const int ty = kBoardRows - (large ? 12 : 8);
  p.w = (int16_t) w;
  p.h = (int16_t) ty;
  const FieldModel &m = p.m;
  const std::string abbr[2] = {g.team_abbr, g.opp_abbr};
  const std::string score[2] = {std::to_string(m.score[0]), std::to_string(m.score[1])};
  const uint32_t white = 0xFFFFFF, down_rgb = m.red ? 0xFF3030 : white;
  const int poss = m.brk ? -1 : g.possession == 1 ? 0 : g.possession == 2 ? 1 : -1;
  std::vector<AppText> &t = out.texts;
  if (w >= 192) {
    const int c = 40, mid_w = w - 2 * c;
    for (int s = 0; s < 2; s++) {
      const int x = s == 0 ? 0 : w - c;
      put(t, x, 2, c, BoardFont::S16, AppAlign::CENTER, abbr_rgb(g, s), abbr[s]);
      put(t, x, large ? 20 : 22, c, BoardFont::S24, AppAlign::CENTER, white, score[s]);
      if (poss == s) {
        const int bw = std::min(c - 4, px_of(abbr[s], BoardFont::S16));
        p.poss_x = (int16_t) (x + (c - bw) / 2);
        p.poss_y = 19;
        p.poss_w = (int16_t) bw;
      }
    }
    p.pip_y = (int16_t) (ty - 6);
    p.pip_x[0] = (c - 16) / 2;
    p.pip_x[1] = (int16_t) (w - 1 - (c - 16) / 2);
    ClockBox b;
    b.x = c;
    b.w = mid_w;
    b.y12 = 0;
    b.y8 = 2;
    place_clock(t, clock, false, b, true);
    p.fx = (int16_t) c;
    p.fw = (int16_t) mid_w;
    p.fy = 13;
    p.fh = (int16_t) (ty - 11 - 13);
    put(t, c, ty - 9, mid_w, BoardFont::S8, AppAlign::CENTER, down_rgb,
        m.spot ? down_row(m, g, mid_w, false) : hidden_row(g, m, mid_w));
  } else if (w >= 128) {
    const int aw[2] = {px_of(abbr[0], BoardFont::S12), px_of(abbr[1], BoardFont::S12)};
    // Smaller scores only when the clock needs room
    for (BoardFont sf : {BoardFont::S16, BoardFont::S12}) {
      const int sw[2] = {px_of(score[0], sf), px_of(score[1], sf)};
      const int left = aw[0] + 4 + sw[0] + 3, gap = w - 4 - aw[1] - sw[1] - 3 - left;
      ClockBox b;
      b.x = b.top_x = b.bot_x = left;
      b.w = b.top_w = b.bot_w = gap;
      b.y12 = 2;
      b.y8 = 4;
      b.bot_y = 8;
      if (!place_clock(t, clock, false, b, sf == BoardFont::S12))
        continue;
      const int sy = sf == BoardFont::S16 ? 0 : 2;
      put(t, aw[0] + 4, sy, sw[0], sf, AppAlign::LEFT, white, score[0]);
      put(t, w - 4 - aw[1] - sw[1], sy, sw[1], sf, AppAlign::RIGHT, white, score[1]);
      break;
    }
    put(t, 1, 2, aw[0], BoardFont::S12, AppAlign::LEFT, abbr_rgb(g, 0), abbr[0]);
    put(t, w - 1 - aw[1], 2, aw[1], BoardFont::S12, AppAlign::RIGHT, abbr_rgb(g, 1), abbr[1]);
    if (poss >= 0) {
      p.poss_x = (int16_t) (poss == 0 ? 1 : w - 1 - aw[1]);
      p.poss_y = 14;
      p.poss_w = (int16_t) aw[poss];
    }
    p.fx = 0;
    p.fw = (int16_t) w;
    p.fy = 17;
    p.fh = (int16_t) (ty - 11 - 17);
    p.pip_y = (int16_t) (ty - 6);
    p.pip_x[0] = 2;
    p.pip_x[1] = (int16_t) (w - 3);
    put(t, 20, ty - 9, w - 40, BoardFont::S8, AppAlign::CENTER, down_rgb,
        m.spot ? down_row(m, g, w - 40, false) : hidden_row(g, m, w - 40));
  } else {
    put(t, 0, 0, 25, BoardFont::S8, AppAlign::LEFT, abbr_rgb(g, 0), abbr[0]);
    put(t, w - 25, 0, 25, BoardFont::S8, AppAlign::RIGHT, abbr_rgb(g, 1), abbr[1]);
    put(t, 0, 9, 24, BoardFont::S12, AppAlign::LEFT, white, score[0]);
    put(t, w - 24, 9, 24, BoardFont::S12, AppAlign::RIGHT, white, score[1]);
    // Period between the names, time between the scores
    const int aw = std::max(px_of(abbr[0], BoardFont::S8), px_of(abbr[1], BoardFont::S8));
    const int sw = std::max(px_of(score[0], BoardFont::S12), px_of(score[1], BoardFont::S12)) + 1;
    ClockBox b;
    b.x = b.bot_x = sw;
    b.w = b.bot_w = w - 2 * sw;
    b.y8 = b.bot_y = 10;
    b.top_x = aw;
    b.top_w = std::max(0, w - 2 * aw);
    place_clock(t, clock, true, b, true);
    if (poss >= 0) {
      const int bw = std::min(25, px_of(abbr[poss], BoardFont::S8)) - 1;
      p.poss_x = (int16_t) (poss == 0 ? 0 : w - bw);
      p.poss_y = 8;
      p.poss_w = (int16_t) bw;
      p.poss_h = 1;
    }
    p.fx = 0;
    p.fw = (int16_t) w;
    p.fy = 22;
    p.fh = (int16_t) (ty - 11 - 22);
    put(t, 0, ty - 9, w, BoardFont::S8, AppAlign::CENTER, down_rgb,
        m.spot ? down_row(m, g, w, true) : hidden_row(g, m, w));
  }
  if (m.brk && m.periods > 0)
    line_score(t, m, p.fx, p.fy, p.fw, p.fh);
  if (w >= 128) {
    p.to[0] = (int8_t) clamp_to(g.team_timeouts);
    p.to[1] = (int8_t) clamp_to(g.opp_timeouts);
  }
  p.numbers = 1;
}

void detail_frame(const GameSnapshot &g, const std::string &clock, int w, bool large, SportFrame &out) {
  out.texts.clear();
  out.pic = FieldPic();
  FieldPic &p = out.pic;
  p.m = field_model(g);
  const FieldModel &m = p.m;
  const int ty = kBoardRows - (large ? 12 : 8);
  const bool narrow = w < 128;
  p.w = (int16_t) w;
  p.h = (int16_t) ty;
  std::vector<AppText> &t = out.texts;
  const std::string row = m.spot ? down_row(m, g, w, narrow) : hidden_row(g, m, w);
  if (!row.empty()) {
    put(t, 0, 0, w, BoardFont::S8, AppAlign::CENTER, m.red ? 0xFF3030 : 0xFFFFFF, row);
  } else {
    ClockBox b;
    b.w = w;
    place_clock(t, clock, false, b, true);
  }
  p.fx = 0;
  p.fw = (int16_t) w;
  p.fy = 9;
  // Field, win bar, the drive, then the chance
  const int rest = ty - 9 - 4 - 9 - 9;
  p.fh = (int16_t) rest;
  const int drive_y = p.fy + p.fh + 4, win_y = drive_y + 9;
  if (m.brk && m.periods > 0) {
    // At a break the line score takes the drive's rows
    line_score(t, m, p.fx, p.fy, p.fw, drive_y + 8 - p.fy);
  } else {
    p.win_y = (int16_t) (p.fy + p.fh + 1);
  }
  const Football &f = g.fb;
  const bool drive = m.spot && f.drive_side == m.side + 1 && !f.drive_over;
  if (drive) {
    // Longest that fits: words, then letters, then no time
    std::string d = field_drive(f, false);
    if (px_of(d, BoardFont::S8) > w || narrow)
      d = field_drive(f, true);
    if (px_of(d, BoardFont::S8) > w && f.drive_secs >= 0)
      d = d.substr(0, d.rfind(' '));
    if (!narrow && px_of("Drive: " + d, BoardFont::S8) <= w)
      d = "Drive: " + d;
    put(t, 0, drive_y, w, BoardFont::S8, AppAlign::CENTER, 0xB4B4B4, d);
  }
  if (m.win >= 0) {
    const std::string a = m.abbr[0], b = m.abbr[1];
    if (narrow) {
      const int s = m.win >= 50 ? 0 : 1;
      put(t, 0, win_y, w, BoardFont::S8, AppAlign::CENTER, m.color[s],
          (s == 0 ? a : b) + " " + std::to_string(s == 0 ? m.win : 100 - m.win) + "%");
    } else {
      put(t, 1, win_y, w / 2, BoardFont::S8, AppAlign::LEFT, m.color[0], a + " " + std::to_string(m.win) + "%");
      put(t, w / 2, win_y, w / 2 - 1, BoardFont::S8, AppAlign::RIGHT, m.color[1],
          std::to_string(100 - m.win) + "% " + b);
    }
  }
  p.numbers = 1;
}

GameSnapshot field_sample(const std::string &team, uint32_t team_rgb, const std::string &opp, uint32_t opp_rgb) {
  GameSnapshot g;
  g.valid = true;
  g.sport = Sport::FOOTBALL;
  g.state = GameState::IN;
  g.status_name = "STATUS_IN_PROGRESS";
  g.period = 3;
  g.display_clock = "8:12";
  g.short_detail = "8:12 - 3rd";
  g.team_abbr = team.empty() ? "HOME" : team;
  g.opp_abbr = opp.empty() || opp == g.team_abbr ? "AWAY" : opp;
  g.team_id = 1;
  g.opp_id = 2;
  char hex[8];
  snprintf(hex, sizeof hex, "%06X", (unsigned) (team.empty() ? 0x14284B : team_rgb & 0xFFFFFF));
  g.team_color = hex;
  snprintf(hex, sizeof hex, "%06X", (unsigned) (opp.empty() ? 0x9E1B32 : opp_rgb & 0xFFFFFF));
  g.opp_color = hex;
  g.team_home = true;
  g.team_score = 17;
  g.opp_score = 14;
  g.possession = 1;
  g.play_side = 1;
  g.team_timeouts = 2;
  g.opp_timeouts = 3;
  g.short_down_distance = "2nd & 7";
  g.down_distance = "2nd & 7 at " + g.opp_abbr + " 34";
  g.last_play = "(8:41) Pass short right for 5 yards to the " + g.opp_abbr + " 34";
  g.play_secs_left = 1692;
  Football &f = g.fb;
  f.yard_line = 66;
  f.down = 2;
  f.distance = 7;
  f.play_start = 61;
  f.play_end = 66;
  f.drive_start = 25;
  f.drive_side = 1;
  f.drive_plays = 6;
  f.drive_yards = 41;
  f.drive_secs = 192;
  f.home_win = 64;
  f.periods = 3;
  const uint8_t line[2][5] = {{7, 3, 7, 0, 0}, {0, 14, 0, 0, 0}};
  memcpy(f.line, line, sizeof line);
  f.leaders = "QB 14/21, 182 YDS, 1 TD | RB 11 CAR, 54 YDS | WR 5 REC, 77 YDS";
  return g;
}

}  // namespace espn
