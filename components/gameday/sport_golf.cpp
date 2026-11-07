// Golf: a tour followed like a team. The board is the tour's current
// tournament from ESPN's scoreboard header (the top 25 in leaderboard order,
// with places, live scores to par and holes played), and the season calendar
// names the next event. Parse, board columns, ticker, Home Assistant's Game
// Status and the winner's splash. Pure, no ESPHome includes.
#include "espn_parse.h"
#include "offseason.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

// The panel builds for speed (-O2). This file runs once a minute at most, on
// the fetch task or to build a line of text, so it builds for size: about
// half the flash. Only GCC has the pragma.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace espn {

using detail::add_part;
using detail::str_or_empty;

static const uint32_t kGolfWhite = 0xFFFFFF;
static const uint32_t kGolfGray = 0xA0A0A0;
static const uint32_t kGolfGold = 0xFFD000;
static const uint32_t kGolfGreen = 0x40FF40;
static const uint32_t kGolfAmber = 0xFFA000;
static const uint32_t kGolfRed = 0xFF3030;  // under par, the golf convention; and a canceled event
static const uint32_t kGolfBlue = 0x40A0FF;  // over par

static const size_t kGolfStatusMax = 12;  // the board's status cell
static const size_t kGolfTitleRow = 25;   // a 128-wide row of spleen_8 (5 px a character)

static std::string cut(const std::string &s, size_t max) { return s.size() > max ? s.substr(0, max) : s; }

// ---- parse --------------------------------------------------------------------

namespace detail {

// Sets each key to true in o: one filter object's fields.
static void keep(JsonObject o, std::initializer_list<const char *> keys) {
  for (const char *k : keys)
    o[k] = true;
}

// The fields kept from a tour's header: its events and their top 25.
void fill_golf_filter(JsonDocument &f) {
  JsonObject ev = f["sports"][0]["leagues"][0]["events"].add<JsonObject>();
  keep(ev, {"id", "date", "endDate", "name", "location", "primary", "broadcast", "period", "summary",
            "defendingChampion", "displayPurse"});
  JsonObject st = ev["fullStatus"].to<JsonObject>();
  keep(st, {"period", "hadPlayoff"});
  keep(st["type"].to<JsonObject>(), {"name", "state", "completed", "shortDetail"});
  JsonObject c = ev["competitors"].add<JsonObject>();
  keep(c, {"order", "place", "lastName", "displayName", "score", "winner"});
  keep(c["status"].to<JsonObject>(), {"thru", "state", "teeTime", "playoff"});
}

// The season calendar only: a scoreboard's events, with every round's
// scores, drop out as they stream past.
void fill_golf_calendar_filter(JsonDocument &f) {
  keep(f["leagues"][0]["calendar"].add<JsonObject>(), {"id", "label", "startDate", "endDate"});
}

// One call per field read, not the lookup inlined at every one (about 4 KB
// of flash on the panel).
__attribute__((noinline)) static JsonVariantConst field(JsonVariantConst v, const char *key) { return v[key]; }
__attribute__((noinline)) static std::string text(JsonVariantConst v, const char *key) {
  return str_or_empty(field(v, key));
}
static int64_t when(JsonVariantConst v, const char *key) { return parse_iso8601_z(text(v, key)); }
__attribute__((noinline)) static bool flag(JsonVariantConst v, const char *key) { return field(v, key) | false; }

// An integer field clamped to [lo, hi]; def when it is missing or not a number.
static int clamp_int(JsonVariantConst v, int lo, int hi, int def) {
  if (!v.is<long long>())
    return def;
  long long n = v.as<long long>();
  return n < lo ? lo : n > hi ? hi : (int) n;
}

static GolfStatus status_of(const std::string &name) {
  static const char *const kNames[] = {"STATUS_SCHEDULED",     "STATUS_IN_PROGRESS", "STATUS_PLAY_COMPLETE",
                                       "STATUS_SUSPENDED",     "STATUS_DELAYED",     "STATUS_FINAL",
                                       "STATUS_CANCELED",      "STATUS_POSTPONED"};
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
    if (name == kNames[i])
      return (GolfStatus) (i + 1);
  return GolfStatus::OTHER;
}

static GolfPhase phase_of(GolfStatus status, const std::string &state) {
  switch (status) {
    case GolfStatus::FINAL:
      return GolfPhase::FINAL;
    case GolfStatus::CANCELED:
    case GolfStatus::POSTPONED:
      return GolfPhase::OFF;
    case GolfStatus::SCHEDULED:
      return GolfPhase::PRE;
    case GolfStatus::IN_PROGRESS:
      return GolfPhase::LIVE;
    case GolfStatus::PLAY_COMPLETE:  // "Round 2 - Play Complete": post with completed true, the event goes on
    case GolfStatus::SUSPENDED:
    case GolfStatus::DELAYED:
      return GolfPhase::BREAK;
    default:
      return state == "in" ? GolfPhase::LIVE : state == "post" ? GolfPhase::BREAK : GolfPhase::PRE;
  }
}

// "-12", "E", "+3". A number field reads as its value.
static void read_score(JsonVariantConst v, GolfRow &r) {
  std::string s;
  if (v.is<long long>()) {
    long long n = v.as<long long>();
    n = n < -999 ? -999 : n > 999 ? 999 : n;
    s = n == 0 ? std::string("E") : (n > 0 ? "+" : "") + std::to_string(n);
  } else {
    s = str_or_empty(v);
  }
  GolfRow::put(r.score, sizeof(r.score), s);
  if (s == "E") {
    r.par_known = true;
    return;
  }
  size_t sign = !s.empty() && (s[0] == '+' || s[0] == '-') ? 1 : 0;
  if (sign >= s.size() || s.find_first_not_of("0123456789", sign) != std::string::npos)
    return;
  long n = s.size() - sign > 3 ? 999 : strtol(s.c_str() + sign, nullptr, 10);
  r.to_par = (int16_t) (s[0] == '-' ? -n : n);
  r.par_known = true;
}

int golf_from_doc(JsonObjectConst root, GameSnapshot &out) {
  JsonVariantConst league = root["sports"][0]["leagues"][0];
  if (league.isNull())
    return -1;
  JsonArrayConst events = field(league, "events");
  if (events.size() == 0)
    return 0;
  // Opposite-field weeks list two events: follow the primary one.
  JsonVariantConst ev = events[0];
  for (JsonVariantConst e : events)
    if (flag(e, "primary")) {
      ev = e;
      break;
    }
  GameSnapshot s;
  s.event_id = text(ev, "id");
  if (s.event_id.empty())
    return -1;
  s.sport = Sport::GOLF;
  s.kickoff_epoch = when(ev, "date");
  Golf &g = s.golf;
  g.end_epoch = when(ev, "endDate");
  s.game_name = ascii_fold(text(ev, "name"));
  s.venue = ascii_fold(text(ev, "location"));
  s.tv = text(ev, "broadcast");
  JsonVariantConst fs = field(ev, "fullStatus");
  JsonVariantConst type = field(fs, "type");
  s.short_detail = text(type, "shortDetail");
  if (s.short_detail.empty())
    s.short_detail = text(ev, "summary");
  s.period = clamp_int(field(fs, "period"), 0, 99, clamp_int(field(ev, "period"), 0, 99, 0));
  g.status = status_of(text(type, "name"));
  g.phase = phase_of(g.status, text(type, "state"));
  s.state = g.phase == GolfPhase::PRE ? GameState::PRE
            : g.phase == GolfPhase::LIVE || g.phase == GolfPhase::BREAK ? GameState::IN
                                                                        : GameState::POST;
  s.completed = g.phase == GolfPhase::FINAL;
  GolfRow::put(g.purse, sizeof(g.purse), text(ev, "displayPurse"));
  g.defending = ascii_fold(text(ev, "defendingChampion"));
  g.playoff = flag(fs, "hadPlayoff");

  // Every listed place, for the ties; the kGolfRows lowest orders, kept
  // sorted as they come (ESPN sends them in order, but the board must not
  // depend on it).
  std::vector<uint8_t> places;
  std::vector<std::pair<int, GolfRow>> kept;
  int index = 0;
  for (JsonVariantConst c : field(ev, "competitors").as<JsonArrayConst>()) {
    index++;
    JsonVariantConst st = field(c, "status");
    if (flag(st, "playoff"))
      g.playoff = true;
    GolfRow r;
    r.place = (uint8_t) clamp_int(field(c, "place"), 0, 255, 0);
    places.push_back(r.place);
    JsonVariantConst order_v = field(c, "order");
    int order = order_v.is<long long>() ? clamp_int(order_v, 0, 1000000, 0) : 1000000 + index;
    auto by_order = [](int o, const std::pair<int, GolfRow> &p) { return o < p.first; };
    auto at = std::upper_bound(kept.begin(), kept.end(), order, by_order);
    if (kept.size() >= kGolfRows && at == kept.end())
      continue;
    std::string name = ascii_fold(text(c, "lastName"));
    if (name.empty())
      name = ascii_fold(text(c, "displayName"));
    GolfRow::put(r.name, sizeof(r.name), name);
    read_score(field(c, "score"), r);
    std::string state = text(st, "state");
    r.state = state == "in" ? 'i' : state == "post" ? 'f' : 'p';
    r.thru = (uint8_t) clamp_int(field(st, "thru"), 0, 18, 0);
    r.tee_epoch = when(st, "teeTime");
    r.winner = flag(c, "winner");
    kept.insert(at, std::make_pair(order, r));
    if (kept.size() > kGolfRows)
      kept.pop_back();
  }
  g.rows.reserve(kept.size());
  for (auto &k : kept) {
    GolfRow &r = k.second;
    if (r.place != 0)
      r.tie = std::count(places.begin(), places.end(), r.place) > 1;
    // ESPN's winner flag was false on the Korn Ferry final: the winner is
    // place 1, held alone.
    r.winner = g.phase == GolfPhase::FINAL && (r.winner || (r.place == 1 && !r.tie));
    g.rows.push_back(r);
  }
  s.valid = true;
  out = std::move(s);
  return 1;
}

int golf_next_from_doc(JsonObjectConst root, int64_t now_epoch, const std::string &after_event, GolfNext &out) {
  JsonVariantConst leagues = root["leagues"];
  if (leagues.isNull())
    return -1;
  GolfNext best;
  for (JsonVariantConst e : field(leagues[0], "calendar").as<JsonArrayConst>()) {
    std::string id = text(e, "id");
    int64_t start = when(e, "startDate");
    if (id.empty() || id == after_event || start <= now_epoch)
      continue;
    if (best.valid && start >= best.start_epoch)
      continue;  // the earliest start; the first listed on a tie
    best.valid = true;
    best.event_id = std::move(id);
    best.name = ascii_fold(text(e, "label"));
    best.start_epoch = start;
    best.end_epoch = when(e, "endDate");
    if (best.end_epoch < start)
      best.end_epoch = start;
  }
  if (!best.valid)
    return 0;
  out = std::move(best);
  return 1;
}

}  // namespace detail

// ---- board text ---------------------------------------------------------------

static bool suspended(const GameSnapshot &s) { return s.golf.status == GolfStatus::SUSPENDED; }
static bool delayed(const GameSnapshot &s) { return s.golf.status == GolfStatus::DELAYED; }
static bool play_complete(const GameSnapshot &s) { return s.golf.status == GolfStatus::PLAY_COMPLETE; }
static const char *off_word(const GameSnapshot &s) {
  return s.golf.status == GolfStatus::POSTPONED ? "Postponed" : "Canceled";
}
// A round number that fits the board's cells: 1 to 9.
static bool short_round(const GameSnapshot &s) { return s.period >= 1 && s.period <= 9; }

std::string golf_round_text(const GameSnapshot &s) {
  const std::string r = std::to_string(s.period);
  switch (s.golf.phase) {
    case GolfPhase::PRE:
      return "Upcoming";
    case GolfPhase::LIVE:
      if (s.golf.playoff)
        return "Playoff";
      return s.period > 0 ? "Round " + r : "Live";
    case GolfPhase::BREAK:
      if (suspended(s))
        return short_round(s) ? "R" + r + " suspended" : "Suspended";
      if (delayed(s))
        return short_round(s) ? "R" + r + " delayed" : "Delayed";
      if (play_complete(s) && s.period > 0)
        return "After R" + r;
      return cut(s.short_detail.empty() ? std::string("Break") : s.short_detail, kGolfStatusMax);
    case GolfPhase::FINAL:
      return "Final";
    case GolfPhase::OFF:
      return off_word(s);
  }
  return "";
}

std::string golf_round_long(const GameSnapshot &s) {
  const std::string r = std::to_string(s.period);
  switch (s.golf.phase) {
    case GolfPhase::PRE:
      return "Upcoming";
    case GolfPhase::LIVE:
      if (s.golf.playoff)
        return "Playoff";
      return s.period > 0 ? "Round " + r : "Live";
    case GolfPhase::BREAK:
      if (suspended(s))
        return s.period > 0 ? "Round " + r + " suspended" : "Suspended";
      if (delayed(s))
        return s.period > 0 ? "Round " + r + " delayed" : "Delayed";
      if (play_complete(s) && s.period > 0)
        return "After round " + r;
      return cut(s.short_detail, 40);
    case GolfPhase::FINAL:
      return "Final";
    case GolfPhase::OFF:
      return off_word(s);
  }
  return "";
}

uint32_t golf_round_color(const GameSnapshot &s) {
  switch (s.golf.phase) {
    case GolfPhase::LIVE:
      return kGolfGreen;
    case GolfPhase::BREAK:
      return suspended(s) || delayed(s) ? kGolfAmber : kGolfWhite;
    case GolfPhase::FINAL:
      return kGolfGold;
    case GolfPhase::OFF:
      return kGolfRed;
    default:
      return kGolfWhite;
  }
}

std::string golf_place_text(const GolfRow &r) {
  if (r.place == 0)
    return "-";
  return (r.tie ? "T" : "") + std::to_string(r.place);
}

std::string golf_pos_text(const GolfRow &r) {
  if (r.place == 0 || r.place > 99)
    return "-";
  if (r.place >= 10)
    return std::to_string(r.place);  // no room for the T
  return golf_place_text(r);
}

std::string golf_score_text(const GolfRow &r) {
  if (!r.par_known)
    return cut(r.score, 3);  // "WD", "DQ": ESPN's own word
  int n = r.to_par < -99 ? -99 : r.to_par > 99 ? 99 : r.to_par;
  if (n == 0)
    return "E";
  return (n > 0 ? "+" : "") + std::to_string(n);
}

uint32_t golf_score_color(const GolfRow &r) {
  if (!r.par_known)
    return kGolfGray;
  return r.to_par < 0 ? kGolfRed : r.to_par > 0 ? kGolfBlue : kGolfWhite;
}

std::string golf_thru_text(const GolfRow &r, GolfPhase phase) {
  if (phase == GolfPhase::FINAL)
    return "";
  switch (r.state) {
    case 'f':
      return "F";
    case 'i':
      return r.thru > 0 ? std::to_string(r.thru) : "-";
    default:
      return r.tee[0] == '\0' ? "-" : cut(r.tee, 6);
  }
}

std::string golf_tee_label(const struct tm &t) {
  if (t.tm_hour < 0 || t.tm_hour > 23 || t.tm_min < 0 || t.tm_min > 59)
    return "";
  int h = t.tm_hour % 12;
  if (h == 0)
    h = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d%c", h, t.tm_min, t.tm_hour < 12 ? 'a' : 'p');
  return buf;
}

std::string golf_dates(const struct tm &a, const struct tm &b) {
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (a.tm_mon < 0 || a.tm_mon > 11 || a.tm_mday < 1 || a.tm_mday > 31)
    return "";
  char buf[24];
  bool one_day = b.tm_mon < 0 || b.tm_mon > 11 || b.tm_mday < 1 || b.tm_mday > 31 ||
                 (a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday);
  if (one_day)
    snprintf(buf, sizeof(buf), "%s %d", kMonths[a.tm_mon], a.tm_mday);
  else if (a.tm_mon == b.tm_mon && a.tm_year == b.tm_year)
    snprintf(buf, sizeof(buf), "%s %d-%d", kMonths[a.tm_mon], a.tm_mday, b.tm_mday);
  else
    snprintf(buf, sizeof(buf), "%s %d-%s %d", kMonths[a.tm_mon], a.tm_mday, kMonths[b.tm_mon], b.tm_mday);
  return buf;
}

static std::string lower(std::string s) {
  for (char &c : s)
    c = (char) tolower((unsigned char) c);
  return s;
}

static void trim_right(std::string &s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == ',' || s.back() == '-' || s.back() == ':'))
    s.pop_back();
}

static std::string last_word(const std::string &s) {
  size_t sp = s.rfind(' ');
  return sp == std::string::npos ? s : s.substr(sp + 1);
}

std::string golf_event_name(const std::string &name, size_t max) {
  std::string s = name;
  // 1. The sponsor tail: "... pres. by Compliance Solutions"
  std::string low = lower(s);
  for (const char *tail : {" presented by ", " pres. by ", " powered by ", " hosted by "}) {
    size_t at = low.find(tail);
    if (at != std::string::npos) {
      s.erase(at);
      low.erase(at);
    }
  }
  trim_right(s);
  if (s.size() <= max)
    return s;
  // 2. One trailing word every event has: "TOTO Japan Classic" is "TOTO Japan"
  static const char *const kGeneric[] = {"championships", "championship", "classic", "invitational",
                                         "challenge",     "tournament"};
  std::string w = lower(last_word(s));
  if (w.size() < s.size())
    for (const char *g : kGeneric)
      if (w == g) {
        s.erase(s.size() - w.size());
        trim_right(s);
        break;
      }
  if (s.size() <= max)
    return s;
  // 3. Whole words up to max, without a dangling "of" or "at the"
  std::string t = s.substr(0, max + 1);
  size_t sp = t.rfind(' ');
  t = sp == std::string::npos ? std::string() : t.substr(0, sp);
  static const char *const kDangling[] = {"of", "at", "the", "and", "&", "-", "by"};
  for (bool dropped = true; dropped && !t.empty();) {
    dropped = false;
    trim_right(t);
    std::string lw = lower(last_word(t));
    if (lw.size() == t.size())
      break;
    for (const char *d : kDangling)
      if (lw == d) {
        t.erase(t.size() - lw.size());
        dropped = true;
        break;
      }
  }
  trim_right(t);
  // 4. One long word: cut it
  return t.empty() ? s.substr(0, max) : t;
}

static bool golf_has_board(const GameSnapshot &s) {
  return s.valid && !s.event_id.empty() &&
         (s.golf.phase == GolfPhase::LIVE || s.golf.phase == GolfPhase::BREAK || s.golf.phase == GolfPhase::FINAL);
}

GolfLines golf_lines(const GameSnapshot &s, bool wide) {
  GolfLines l;
  l.status = golf_round_text(s);
  l.status_color = golf_round_color(s);
  if (wide && l.status.size() + 1 < kGolfTitleRow)
    l.title = golf_event_name(s.game_name, kGolfTitleRow - 1 - l.status.size());
  if (!golf_has_board(s))
    return l;  // the card (golf_card_text)
  const size_t name_max = wide ? 11 : 7;
  for (size_t i = 0; i < s.golf.rows.size() && i < kGolfBoardRows; i++) {
    const GolfRow &r = s.golf.rows[i];
    l.pos.push_back(golf_pos_text(r));
    l.name.push_back(cut(r.name, name_max));
    l.score.push_back(golf_score_text(r));
    l.thru.push_back(wide ? golf_thru_text(r, s.golf.phase) : std::string());
    l.score_color.push_back((int) golf_score_color(r));
    l.name_color.push_back((int) (r.winner ? kGolfGold : kGolfWhite));
  }
  return l;
}

std::string golf_card_text(const GameSnapshot &s, bool wide, const std::string &dates) {
  std::string out = golf_event_name(s.game_name, wide ? 42 : 36);
  std::string second;
  if (s.event_id.empty())
    second = "";  // the tour's name while the first read runs
  else if (s.golf.phase == GolfPhase::PRE)
    second = dates;
  else if (s.golf.phase == GolfPhase::OFF)
    second = off_word(s);
  else
    second = golf_round_text(s);  // in play with nobody listed yet
  if (!second.empty())
    out += "\n" + second;
  return out;
}

// ---- ticker and Home Assistant ---------------------------------------------------

static const GolfRow *winner_row(const GameSnapshot &s) {
  if (s.golf.phase != GolfPhase::FINAL)
    return nullptr;
  for (const auto &r : s.golf.rows)
    if (r.winner)
      return &r;
  return nullptr;
}

// "T1 Iwai -12 thru 7", "3 Hara -11 F", "T6 Glover -10"
static std::string ticker_row(const GolfRow &r, bool thru) {
  char buf[48];
  int n = snprintf(buf, sizeof(buf), "%s %s %s", golf_place_text(r).c_str(), r.name, golf_score_text(r).c_str());
  if (n > 0 && (size_t) n < sizeof(buf) && thru && (r.state == 'f' || (r.state == 'i' && r.thru > 0)))
    snprintf(buf + n, sizeof(buf) - n, r.state == 'f' ? " F" : " thru %u", (unsigned) r.thru);
  return buf;
}

static std::string ticker_rows(const GameSnapshot &s, size_t from, size_t to, bool thru) {
  std::string out;
  for (size_t i = from; i < s.golf.rows.size() && i < to; i++) {
    if (!out.empty())
      out += ", ";
    out += ticker_row(s.golf.rows[i], thru);
  }
  return out;
}

static std::string wins_line(const GolfRow &w, bool playoff) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s wins %sat %s", w.name, playoff ? "in a playoff " : "", golf_score_text(w).c_str());
  return buf;
}

static void add_next(std::string &out, const GameSnapshot &s, const std::string &dates) {
  if (!s.golf.next.valid)
    return;
  add_part(out, "Next: " + s.golf.next.name + (dates.empty() ? "" : ", " + dates));
}

std::string golf_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &dates) {
  if (!s.valid || s.state == GameState::NOT_FOUND)
    return "No upcoming event";
  const Golf &g = s.golf;
  std::string out = s.game_name;
  switch (g.phase) {
    case GolfPhase::PRE:
      add_part(out, dates);
      add_part(out, s.venue);
      if (o.odds && g.purse[0] != '\0')
        add_part(out, std::string("Purse ") + g.purse);
      if (!g.defending.empty())
        add_part(out, "Defending champion: " + g.defending);
      if (o.odds)
        add_part(out, s.tv);
      return out;
    case GolfPhase::LIVE:
      add_part(out, golf_round_long(s));
      add_part(out, ticker_rows(s, 0, kGolfRows, true));
      if (o.odds)
        add_part(out, s.tv);
      return out;
    case GolfPhase::BREAK:
      add_part(out, golf_round_long(s));
      add_part(out, ticker_rows(s, 0, kGolfRows, false));
      if (!g.rows.empty() && g.rows[0].state == 'p' && g.rows[0].tee[0] != '\0')
        add_part(out, std::string("Leaders tee off ") + g.rows[0].tee);
      if (o.odds)
        add_part(out, s.tv);
      return out;
    case GolfPhase::FINAL: {
      add_part(out, "Final");
      const GolfRow *w = winner_row(s);
      if (w != nullptr) {
        add_part(out, wins_line(*w, g.playoff));
        add_part(out, ticker_rows(s, w == &g.rows[0] ? 1 : 0, 5, false));
      } else {
        add_part(out, ticker_rows(s, 0, 5, false));
      }
      add_next(out, s, dates);
      return out;
    }
    case GolfPhase::OFF:
      add_part(out, off_word(s));
      add_next(out, s, dates);
      return out;
  }
  return out;
}

std::string golf_ha_status_text(const GameSnapshot &s, const std::string &dates) {
  if (!s.valid || s.state == GameState::NOT_FOUND)
    return "No upcoming event";
  const Golf &g = s.golf;
  std::string out = s.game_name;
  switch (g.phase) {
    case GolfPhase::PRE:
      add_part(out, dates);
      return out;
    case GolfPhase::LIVE:
    case GolfPhase::BREAK:
      add_part(out, golf_round_long(s));
      if (!g.rows.empty())
        add_part(out, std::string("Leader: ") + g.rows[0].name + " " + golf_score_text(g.rows[0]));
      return out;
    case GolfPhase::FINAL: {
      add_part(out, "Final");
      const GolfRow *w = winner_row(s);
      if (w != nullptr)
        add_part(out, wins_line(*w, g.playoff));
      return out;
    }
    case GolfPhase::OFF:
      add_part(out, off_word(s));
      return out;
  }
  return out;
}

Splash golf_splash(const GameSnapshot &prev, const GameSnapshot &cur) {
  if (!detail::same_game(prev, cur) || prev.sport != Sport::GOLF || cur.sport != Sport::GOLF)
    return Splash{};
  if (prev.golf.phase != GolfPhase::LIVE && prev.golf.phase != GolfPhase::BREAK)
    return Splash{};
  const GolfRow *w = winner_row(cur);
  if (w == nullptr || w->name[0] == '\0')
    return Splash{};
  std::string name = w->name;
  for (char &c : name)
    c = (char) toupper((unsigned char) c);
  return Splash{name + " WINS!", kGolfGold};
}

// ---- timing ---------------------------------------------------------------------

GameSnapshot golf_next_snapshot(const GolfNext &n) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.sport = Sport::GOLF;
  g.event_id = n.event_id;
  g.game_name = n.name;
  g.kickoff_epoch = n.start_epoch;
  g.golf.end_epoch = n.end_epoch;
  g.golf.phase = GolfPhase::PRE;
  g.golf.status = GolfStatus::SCHEDULED;
  return g;
}

// Where an event is: before it, under way, or over.
static int phase_rank(GolfPhase p) {
  switch (p) {
    case GolfPhase::PRE:
      return 0;
    case GolfPhase::LIVE:
    case GolfPhase::BREAK:
      return 1;
    default:
      return 2;
  }
}

bool golf_read_is_older(const GameSnapshot &shown, const GameSnapshot &read) {
  if (!shown.valid || !read.valid || shown.sport != Sport::GOLF || read.sport != Sport::GOLF ||
      shown.event_id.empty())
    return false;
  if (shown.event_id != read.event_id)
    return read.kickoff_epoch < shown.kickoff_epoch;
  if (read.period < shown.period)
    return true;
  return phase_rank(read.golf.phase) < phase_rank(shown.golf.phase);
}

static int64_t last_day(const GameSnapshot &g) { return g.golf.end_epoch > 0 ? g.golf.end_epoch : g.kickoff_epoch; }

uint32_t golf_poll_ms(const GameSnapshot &g, int64_t now) {
  const uint32_t minute = 60 * 1000;
  switch (g.golf.phase) {
    case GolfPhase::LIVE:
      return minute;
    case GolfPhase::BREAK:
      return 5 * minute;
    case GolfPhase::PRE: {
      int64_t until = g.kickoff_epoch - now;
      if (until <= 12 * 3600)
        return 5 * minute;  // the field and tee times can come any time now
      return until > kSeasonHorizon ? 360 * minute : 30 * minute;
    }
    default:
      // Waiting for ESPN to move its header on to the next event, usually a
      // day or two; between seasons, a few times a day.
      return now - last_day(g) <= kSeasonHorizon ? 30 * minute : 360 * minute;
  }
}

bool golf_off_season(const GameSnapshot &g, int64_t now) {
  if (!g.valid || g.sport != Sport::GOLF || g.event_id.empty())
    return false;
  if (g.golf.phase == GolfPhase::PRE)
    return g.kickoff_epoch - now > kSeasonHorizon;
  if (g.golf.phase != GolfPhase::FINAL && g.golf.phase != GolfPhase::OFF)
    return false;
  if (now - last_day(g) <= kSeasonHorizon)
    return false;
  return !g.golf.next.valid || g.golf.next.start_epoch - now > kSeasonHorizon;
}

bool golf_show_next(const GameSnapshot &g, int64_t now) {
  return g.valid && g.sport == Sport::GOLF && g.golf.next.valid &&
         (g.golf.phase == GolfPhase::FINAL || g.golf.phase == GolfPhase::OFF) && now - last_day(g) > kSeasonHorizon;
}

}  // namespace espn
