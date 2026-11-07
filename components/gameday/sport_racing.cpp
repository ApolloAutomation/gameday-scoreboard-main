// Racing: a series (Formula 1, the NASCAR Cup Series) followed like a golf
// tour. The board is the series' race weekend from ESPN's scoreboard header,
// one entry per session (practice, qualifying, sprint, race): the session on
// track or the latest one's order, the race's winner, and before and between
// weekends the next session or race. The season calendar names the next race
// weekend. Parse, board columns, ticker, Home Assistant's Game Status and the
// winner's splash, on golf's shared helpers. Pure, no ESPHome includes.
#include "espn_parse.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Built for size, as sport_golf.cpp is: it runs every 30 seconds at most.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace espn {

using detail::add_part;
using detail::cut;
using detail::fit_words;
using detail::last_word;
using detail::trim_right;
using detail::whole_words;

static const uint32_t kRaceWhite = 0xFFFFFF;
static const uint32_t kRaceGold = 0xFFD000;
static const uint32_t kRaceGray = 0xA0A0A0;
static const uint32_t kRaceGreen = 0x40FF40;
static const uint32_t kRaceAmber = 0xFFA000;
static const uint32_t kRaceRed = 0xFF3030;

static const size_t kRaceStatusMax = 12;  // the board's status cell
static const size_t kRaceSessions = 16;   // header entries read; an F1 weekend has five
static const int64_t kRaceHour = 3600;
static const int64_t kRaceDay = 24 * kRaceHour;
// The calendar's times run three hours late against the header's, the same
// for every race (an Eastern clock read as Pacific): Sao Paulo's first
// practice is 15:30Z in the header and 18:30Z in the calendar.
static const int64_t kCalendarSkew = 3 * kRaceHour;

// ---- parse --------------------------------------------------------------------

namespace detail {

// The fields kept from a series' header: every session of the listed
// weekends and their order.
void fill_racing_filter(JsonDocument &f) {
  JsonObject ev = f["sports"][0]["leagues"][0]["events"].add<JsonObject>();
  keep(ev, {"id", "date", "name", "location", "laps", "broadcast"});
  keep(ev["competitionType"].to<JsonObject>(), {"id", "abbreviation"});
  JsonObject st = ev["fullStatus"].to<JsonObject>();
  keep(st, {"period", "flag"});
  keep(st["type"].to<JsonObject>(), {"name", "state", "shortDetail"});
  JsonObject c = ev["competitors"].add<JsonObject>();
  keep(c, {"order", "place", "lastName", "displayName", "winner", "behindTime", "behindLaps", "time"});
  keep(c["vehicle"].to<JsonObject>(), {"number"});
  keep(c["status"]["type"].to<JsonObject>(), {"name"});
}

// The season calendar only. Its entries have no id: the event's is in $ref.
void fill_racing_calendar_filter(JsonDocument &f) {
  JsonObject e = f["leagues"][0]["calendar"].add<JsonObject>();
  keep(e, {"label", "startDate", "endDate"});
  keep(e["event"].to<JsonObject>(), {"$ref"});
}

// A number ESPN sends as a number or as text ("56", "-13"), in +-1000000;
// def when it is neither.
static long whole(JsonVariantConst v, long def) {
  long n;
  if (v.is<long long>()) {
    long long x = v.as<long long>();
    n = x < -1000000 ? -1000000 : x > 1000000 ? 1000000 : (long) x;
    return n;
  }
  const char *s = v.as<const char *>();
  if (s == nullptr)
    return def;
  char *end = nullptr;
  n = strtol(s, &end, 10);
  if (end == s)
    return def;
  return n < -1000000 ? -1000000 : n > 1000000 ? 1000000 : n;
}

// A lap or race time in milliseconds: "1:10.763", "58.1", "1:35:16.650". -1
// for anything else ("-1:27.798" on a disqualified row, "DNF").
static int32_t time_ms(const std::string &s) {
  int64_t total = 0, part = 0;
  int digits = 0, colons = 0;
  size_t i = 0;
  for (; i < s.size() && s[i] != '.'; i++) {
    char c = s[i];
    if (c == ':') {
      if (digits == 0 || ++colons > 2)
        return -1;
      total = total * 60 + part;
      part = 0;
      digits = 0;
      continue;
    }
    if (c < '0' || c > '9' || ++digits > 3)
      return -1;
    part = part * 10 + (c - '0');
  }
  if (digits == 0)
    return -1;
  total = total * 60 + part;
  int ms = 0, frac = 0;
  if (i < s.size()) {
    for (i++; i < s.size(); i++) {
      char c = s[i];
      if (c < '0' || c > '9')
        return -1;
      if (frac < 3) {
        ms = ms * 10 + (c - '0');
        frac++;
      }
    }
    if (frac == 0)
      return -1;
    for (; frac < 3; frac++)
      ms *= 10;
  }
  total = total * 1000 + ms;
  return total > INT32_MAX ? -1 : (int32_t) total;
}

// "+0.253", "+1:02.004"
static std::string gap_text(int32_t d) {
  char buf[32];
  if (d < 60000)
    snprintf(buf, sizeof(buf), "+%d.%03d", (int) (d / 1000), (int) (d % 1000));
  else
    snprintf(buf, sizeof(buf), "+%d:%02d.%03d", (int) (d / 60000), (int) (d / 1000 % 60), (int) (d % 1000));
  return buf;
}

// One header entry: a session of a race weekend.
struct Session {
  JsonVariantConst ev;
  const char *id{""};  // the weekend's event id, the same for each of its sessions
  int64_t start{0};
  RaceKind kind{RaceKind::RACE};
  uint8_t number{0};  // practice 1 to 3
  GolfStatus status{GolfStatus::OTHER};
  char st{'p'};       // 'p' to come, 'i' on track, 'f' over, 'x' canceled or postponed
  size_t rows{0};     // competitors listed
};

// By competitionType: 1 practice ("FP2"), 2 qualifying, 3 the race (4 a
// NASCAR exhibition race), 5 the sprint shootout, 6 the sprint. NASCAR lists
// only its race, with no type. Another type is no race.
static RaceKind kind_of(JsonVariantConst ct, uint8_t &number) {
  number = 0;
  if (ct.isNull())
    return RaceKind::RACE;
  switch (whole(field(ct, "id"), 3)) {
    case 1: {
      std::string ab = text(ct, "abbreviation");
      if (!ab.empty() && ab.back() >= '1' && ab.back() <= '9')
        number = (uint8_t) (ab.back() - '0');
      return RaceKind::PRACTICE;
    }
    case 2:
      return RaceKind::QUALIFYING;
    case 3:
    case 4:
      return RaceKind::RACE;
    case 5:
      return RaceKind::SPRINT_QUALIFYING;
    case 6:
      return RaceKind::SPRINT;
    default:
      return RaceKind::PRACTICE;
  }
}

static char state_of(GolfStatus status, const std::string &state) {
  if (status == GolfStatus::CANCELED || status == GolfStatus::POSTPONED)
    return 'x';
  // Stopped or held up, it is still the session on: golf's suspended round
  // is over for the day, a race's red flag is not.
  if (status == GolfStatus::SUSPENDED || status == GolfStatus::DELAYED)
    return 'i';
  return state == "in" ? 'i' : state == "post" ? 'f' : 'p';
}

static bool timed(RaceKind k) {
  return k == RaceKind::PRACTICE || k == RaceKind::QUALIFYING || k == RaceKind::SPRINT_QUALIFYING;
}

int racing_from_doc(JsonObjectConst root, GameSnapshot &out) {
  JsonVariantConst league = root["sports"][0]["leagues"][0];
  if (league.isNull())
    return -1;
  JsonArrayConst events = field(league, "events");
  if (events.size() == 0)
    return 0;
  // On the fetch task's stack: no copy of a session's strings, which stay in
  // the document.
  Session all[kRaceSessions];
  size_t count = 0;
  for (JsonVariantConst e : events) {
    if (count >= kRaceSessions)
      break;
    Session &s = all[count++];
    s.ev = e;
    const char *id = field(e, "id").as<const char *>();
    s.id = id != nullptr ? id : "";
    s.start = when(e, "date");
    s.kind = kind_of(field(e, "competitionType"), s.number);
    JsonVariantConst type = field(field(e, "fullStatus"), "type");
    s.status = status_of(text(type, "name"));
    s.st = state_of(s.status, text(type, "state"));
    s.rows = field(e, "competitors").size();
  }
  // The weekend (a NASCAR day can list two races, each its own event): the
  // one on track, else the one whose last session with results ran latest
  // (so the first Duel's final stays up until the second starts), else the
  // one starting first.
  const Session *pick = nullptr;
  for (size_t i = 0; i < count && pick == nullptr; i++)
    if (all[i].st == 'i')
      pick = &all[i];
  for (size_t i = 0; i < count; i++)
    if (all[i].st == 'f' && all[i].rows > 0 && (pick == nullptr || (pick->st == 'f' && all[i].start > pick->start)))
      pick = &all[i];
  for (size_t i = 0; i < count; i++)
    if (all[i].st == 'p' && (pick == nullptr || (pick->st == 'p' && all[i].start < pick->start)))
      pick = &all[i];
  if (pick == nullptr)
    pick = &all[0];
  const char *id = pick->id;
  if (id[0] == '\0')
    return -1;
  // Its sessions by start, in the order listed on a tie
  const Session *g[kRaceSessions];
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(all[i].id, id) != 0)
      continue;
    size_t at = n++;
    for (; at > 0 && g[at - 1]->start > all[i].start; at--)
      g[at] = g[at - 1];
    g[at] = &all[i];
  }

  // The session on track, else the last with results, is on the board. The
  // next is the first to come after it; the race is the last race-type
  // session (never a practice, qualifying or sprint).
  const Session *live = nullptr, *shown = nullptr, *next = nullptr, *race = nullptr;
  bool all_off = true;
  for (size_t i = 0; i < n; i++) {
    const Session *s = g[i];
    if (s->st == 'i')
      live = s;
    if (s->st == 'f' && s->rows > 0)
      shown = s;
    if (s->kind == RaceKind::RACE)
      race = s;
    if (s->st != 'x')
      all_off = false;
  }
  if (live != nullptr)
    shown = live;
  for (size_t i = 0; i < n && next == nullptr; i++)
    if (g[i]->st == 'p' && (shown == nullptr || g[i]->start >= shown->start))
      next = g[i];
  const Session *race_or_last = race != nullptr ? race : g[n - 1];
  RacePhase phase;
  if ((race != nullptr && race->st == 'x') || all_off)
    phase = RacePhase::OFF;
  else if (shown == nullptr)
    phase = RacePhase::PRE;
  else if (shown->st == 'i')
    phase = RacePhase::LIVE;
  else if (shown == race)
    phase = RacePhase::FINAL;
  else
    phase = RacePhase::BREAK;
  const Session *ref = shown != nullptr ? shown : next != nullptr ? next : race_or_last;
  JsonVariantConst ev = ref->ev;

  GameSnapshot s;
  s.sport = Sport::RACING;
  s.event_id = id;
  s.kickoff_epoch = g[0]->start;
  s.game_name = ascii_fold(text(ev, "name"));
  s.venue = ascii_fold(text(ev, "location"));
  s.tv = text(ev, "broadcast");
  JsonVariantConst fs = field(ev, "fullStatus");
  s.short_detail = ascii_fold(text(field(fs, "type"), "shortDetail"));
  s.period = clamp_int(field(fs, "period"), 0, 999, 0);
  Racing &r = s.race;
  r.phase = phase;
  r.status = phase == RacePhase::OFF ? race_or_last->status : ref->status;
  r.kind = ref->kind;
  r.number = ref->number;
  long laps = whole(field(ev, "laps"), 0);
  r.laps = (uint16_t) (laps < 0 ? 0 : laps > 999 ? 999 : laps);
  std::string fl = text(fs, "flag");  // "CHECKER" on a finished session
  for (char &c : fl)
    c = (char) toupper((unsigned char) c);
  if (fl.compare(0, 3, "RED") == 0)
    r.flag = 'R';
  else if (fl.compare(0, 6, "YELLOW") == 0 || fl.compare(0, 7, "CAUTION") == 0)
    r.flag = 'Y';
  r.session_start = shown != nullptr ? shown->start : 0;
  r.race_epoch = race_or_last->start;
  if (next != nullptr) {
    r.next_kind = next->kind;
    r.next_number = next->number;
    r.next_start = next->start;
  }
  s.state = phase == RacePhase::PRE ? GameState::PRE
            : phase == RacePhase::LIVE || phase == RacePhase::BREAK ? GameState::IN
                                                                    : GameState::POST;
  s.completed = phase == RacePhase::FINAL;

  // The shown session's kRaceRows lowest orders, kept sorted as they come.
  if (shown != nullptr) {
    const bool by_time = timed(shown->kind);
    struct Kept {
      int order;
      int32_t ms;  // a timed session's lap time, -1 for none
      RaceRow row;
    };
    Kept kept[kRaceRows + 1];
    size_t nk = 0;
    int index = 0;
    for (JsonVariantConst c : field(shown->ev, "competitors").as<JsonArrayConst>()) {
      index++;
      JsonVariantConst order_v = field(c, "order");
      int order = order_v.is<long long>() ? clamp_int(order_v, 0, 1000000, 0) : 1000000 + index;
      size_t at = nk;
      while (at > 0 && order < kept[at - 1].order)
        at--;
      if (at >= kRaceRows)
        continue;
      Kept k{order, -1, RaceRow{}};
      RaceRow &row = k.row;
      row.place = (uint8_t) clamp_int(field(c, "place"), 0, 255, 0);
      std::string name = ascii_fold(text(c, "lastName"));
      if (name.empty())
        name = ascii_fold(text(c, "displayName"));
      GolfRow::put(row.name, sizeof(row.name), name);
      GolfRow::put(row.num, sizeof(row.num), text(field(c, "vehicle"), "number"));
      std::string status = text(field(field(c, "status"), "type"), "name");
      row.out = status == "STATUS_RETIRED" ? 'R' : status == "STATUS_DISQUALIFIED" ? 'D' : 0;
      row.winner = flag(c, "winner");
      std::string gap;
      if (by_time) {
        // ESPN's behindTime there is wrong in a sprint shootout (the
        // driver's own time) and missing in some practices: the gap comes
        // from the lap times below.
        gap = text(c, "time");
        k.ms = time_ms(gap);
        if (k.ms < 0)
          gap.clear();
      } else {
        long down = whole(field(c, "behindLaps"), 0);  // NASCAR's is negative
        down = down < 0 ? -down : down;
        row.laps_down = (uint16_t) (down > 999 ? 999 : down);
        gap = text(c, "behindTime");  // NASCAR's is always ".000"
        if (gap.empty() || gap[0] != '+')
          gap.clear();
      }
      GolfRow::put(row.gap, sizeof(row.gap), gap);
      for (size_t i = nk; i > at; i--)
        kept[i] = kept[i - 1];
      kept[at] = k;
      if (nk < kRaceRows)
        nk++;
    }
    // A timed session: the leader keeps its own time, the rest the gap to it.
    int32_t lead = -1;
    r.rows.reserve(nk);
    for (size_t i = 0; i < nk; i++) {
      Kept &k = kept[i];
      if (by_time && k.ms >= 0) {
        if (lead < 0)
          lead = k.ms;
        else
          GolfRow::put(k.row.gap, sizeof(k.row.gap), k.ms >= lead ? gap_text(k.ms - lead) : std::string());
      }
      k.row.winner = phase == RacePhase::FINAL && (k.row.winner || k.row.place == 1);
      r.rows.push_back(k.row);
    }
  }
  s.valid = true;
  out = std::move(s);
  return 1;
}

int racing_next_from_doc(JsonObjectConst root, int64_t now_epoch, const std::string &after_event, GolfNext &out) {
  JsonVariantConst leagues = root["leagues"];
  if (leagues.isNull())
    return -1;
  GolfNext best;
  for (JsonVariantConst e : field(leagues[0], "calendar").as<JsonArrayConst>()) {
    std::string ref = text(field(e, "event"), "$ref");  // ".../events/600057449?lang=en"
    std::string id;
    size_t at = ref.find("/events/");
    if (at != std::string::npos) {
      size_t from = at + 8, to = from;
      while (to < ref.size() && isdigit((unsigned char) ref[to]))
        to++;
      id = ref.substr(from, to - from);
    }
    int64_t start = when(e, "startDate") - kCalendarSkew;
    int64_t end = when(e, "endDate") - kCalendarSkew;
    if (end < start)
      end = start;
    // Over once its race (the weekend's end) started six hours ago.
    if (id.empty() || id == after_event || end + 6 * kRaceHour <= now_epoch)
      continue;
    if (best.valid && start >= best.start_epoch)
      continue;  // the earliest start; the first listed on a tie
    best.valid = true;
    best.event_id = std::move(id);
    best.name = ascii_fold(text(e, "label"));
    best.start_epoch = start;
    best.end_epoch = end;
  }
  if (!best.valid)
    return 0;
  out = std::move(best);
  return 1;
}

}  // namespace detail

std::string racing_gap(const std::string &time, const std::string &leader) {
  int32_t t = detail::time_ms(time), l = detail::time_ms(leader);
  if (t < 0 || l < 0 || t < l)
    return "";
  return detail::gap_text(t - l);
}

// ---- board text ---------------------------------------------------------------

std::string racing_session_label(RaceKind kind, uint8_t number) {
  switch (kind) {
    case RaceKind::PRACTICE:
      return number >= 1 && number <= 9 ? "FP" + std::to_string(number) : std::string("Practice");
    case RaceKind::QUALIFYING:
      return "Qual";
    case RaceKind::SPRINT_QUALIFYING:
      return "Sprint Q";
    case RaceKind::SPRINT:
      return "Sprint";
    default:
      return "Race";
  }
}

std::string racing_session_long(RaceKind kind, uint8_t number) {
  switch (kind) {
    case RaceKind::PRACTICE:
      return number >= 1 && number <= 9 ? "Practice " + std::to_string(number) : std::string("Practice");
    case RaceKind::QUALIFYING:
      return "Qualifying";
    case RaceKind::SPRINT_QUALIFYING:
      return "Sprint qualifying";
    default:
      return racing_session_label(kind, number);
  }
}

static bool race_like(RaceKind k) { return k == RaceKind::RACE || k == RaceKind::SPRINT; }
static bool held(const GameSnapshot &s) {
  return s.race.status == GolfStatus::SUSPENDED || s.race.status == GolfStatus::DELAYED;
}
// Stopped six hours past its start: a race put off overnight, not a red flag
static bool held_long(const GameSnapshot &s, int64_t now) {
  return held(s) && now - s.race.session_start > 6 * kRaceHour;
}
static const char *held_word(const GameSnapshot &s) {
  return s.race.status == GolfStatus::SUSPENDED ? "Suspended" : "Delayed";
}
static const char *off_word(const GameSnapshot &s) {
  return s.race.status == GolfStatus::POSTPONED ? "Postponed" : "Canceled";
}
// "label final" or "label live", or the label alone when that won't fit
static std::string with_word(const std::string &label, const char *word) {
  std::string t = label + " " + word;
  return t.size() <= kRaceStatusMax ? t : label;
}

std::string racing_state_text(const GameSnapshot &s) {
  const Racing &r = s.race;
  switch (r.phase) {
    case RacePhase::PRE:
      return "Upcoming";
    case RacePhase::LIVE: {
      if (r.flag == 'R')
        return "Red flag";
      if (held(s))
        return held_word(s);
      if (race_like(r.kind)) {
        // "Lap 34/56", "Sprint 5/19", or the lap alone with no room for the total
        if (s.period <= 0)
          return r.kind == RaceKind::RACE ? "Race live" : "Sprint live";
        std::string t = std::string(r.kind == RaceKind::RACE ? "Lap " : "Sprint ") + std::to_string(s.period);
        std::string of = r.laps > 0 ? t + "/" + std::to_string(r.laps) : t;
        return of.size() <= kRaceStatusMax ? of : t;
      }
      return with_word(racing_session_label(r.kind, r.number), "live");
    }
    case RacePhase::BREAK:
      return with_word(racing_session_label(r.kind, r.number), "final");
    case RacePhase::FINAL:
      return "Final";
    case RacePhase::OFF:
      return off_word(s);
  }
  return "";
}

std::string racing_state_long(const GameSnapshot &s) {
  const Racing &r = s.race;
  const std::string label = racing_session_long(r.kind, r.number);
  switch (r.phase) {
    case RacePhase::PRE:
      return "Upcoming";
    case RacePhase::LIVE: {
      std::string t;
      if (held(s)) {
        t = label + (s.race.status == GolfStatus::SUSPENDED ? " suspended" : " delayed");
      } else if (race_like(r.kind) && s.period > 0) {
        t = label + " lap " + std::to_string(s.period);
        if (r.laps > 0)
          t += "/" + std::to_string(r.laps);
      } else {
        t = label + " live";
      }
      if (r.flag == 'R')
        t += ", red flag";
      else if (r.flag == 'Y')
        t += ", yellow flag";
      return t;
    }
    case RacePhase::BREAK:
      return label + " final";
    case RacePhase::FINAL:
      return "Final";
    case RacePhase::OFF:
      return off_word(s);
  }
  return "";
}

uint32_t racing_state_color(const GameSnapshot &s) {
  switch (s.race.phase) {
    case RacePhase::LIVE:
      if (s.race.flag == 'R')
        return kRaceRed;
      return s.race.flag == 'Y' || held(s) ? kRaceAmber : kRaceGreen;
    case RacePhase::FINAL:
      return kRaceGold;
    case RacePhase::OFF:
      return kRaceRed;
    default:
      return kRaceWhite;
  }
}

std::string racing_pos_text(const RaceRow &r) {
  return r.place >= 1 && r.place <= 99 ? std::to_string(r.place) : std::string("-");
}

// "Stenhouse Jr." is "Stenhouse"
static std::string no_suffix(std::string n) {
  for (const char *suffix : {" Jr.", " Sr.", " III", " II"}) {
    size_t len = strlen(suffix);
    if (n.size() > len && n.compare(n.size() - len, len, suffix) == 0) {
      n.erase(n.size() - len);
      break;
    }
  }
  return n;
}

std::string racing_name_fit(const std::string &name, size_t max) {
  if (name.size() <= max)
    return name;
  std::string n = no_suffix(name);
  // "van Gisbergen" is "Gisbergen"
  while (n.size() > max && !n.empty() && islower((unsigned char) n[0]) && n.find(' ') != std::string::npos)
    n.erase(0, n.find(' ') + 1);
  return cut(n, max);
}

std::string racing_detail_text(const RaceRow &r) {
  if (r.out == 'R')
    return "DNF";
  if (r.out == 'D')
    return "DSQ";
  if (r.laps_down > 0)
    return "+" + std::to_string(r.laps_down) + "L";
  std::string g = cut(r.gap, 6);  // "+13.07", "+1:20", "1:10.7"
  while (!g.empty() && g.back() == '.')
    g.pop_back();
  return g;
}

std::string racing_detail_long(const RaceRow &r) {
  if (r.out == 'R')
    return "DNF";
  if (r.out == 'D')
    return "DSQ";
  if (r.laps_down > 0)
    return "+" + std::to_string(r.laps_down) + (r.laps_down == 1 ? " lap" : " laps");
  return r.gap;
}

// F1's two-word places. A name's place is its last word otherwise ("MSC
// Cruises Belgian Grand Prix" is "Belgian GP").
static const char *const kTwoWordPlaces[] = {"Sao Paulo",     "Mexico City",    "Las Vegas",
                                             "Abu Dhabi",     "Saudi Arabian",  "Emilia Romagna",
                                             "Buenos Aires",  "South African",  "Great Britain"};

std::string racing_event_name(const std::string &name, size_t max) {
  std::string s = name;
  // 1. NASCAR's series name: "NASCAR Cup Series at Miami" is "Miami"
  for (const char *series : {"NASCAR Cup Series at ", "NASCAR Cup Series "}) {
    size_t len = strlen(series);
    if (s.size() > len && s.compare(0, len, series) == 0) {
      s.erase(0, len);
      break;
    }
  }
  // 2. F1's place and "GP", without the sponsor in front: "MSC Cruises Sao
  // Paulo Grand Prix" is "Sao Paulo GP"
  size_t gp = s.find("Grand Prix");
  if (gp != std::string::npos) {
    std::string place = s.substr(0, gp);  // "Gulf Air Bahrain Grand Prix in Malaysia": no " in" tail
    trim_right(place);
    size_t us = place.find("United States");
    if (us != std::string::npos)
      place.replace(us, 13, "US");
    std::string word = last_word(place);
    size_t sp = place.rfind(' ');
    if (sp != std::string::npos) {
      std::string two = last_word(place.substr(0, sp)) + " " + word;
      for (const char *p : kTwoWordPlaces)
        if (two == p)
          word = two;
    }
    if (!word.empty())
      s = word + " GP";
  }
  if (s.size() <= max)
    return s;
  // 3. Without the "GP", then a hyphenated place's first part
  // ("Barcelona-Catalunya" is "Barcelona"), then whole words
  if (s.size() > 3 && s.compare(s.size() - 3, 3, " GP") == 0)
    s.erase(s.size() - 3);
  if (s.size() <= max)
    return s;
  size_t hyphen = s.find('-');
  if (hyphen != std::string::npos && hyphen > 0) {
    s.erase(hyphen);
    trim_right(s);
  }
  return whole_words(s, max);
}

std::string racing_when(const struct tm &start_local, const struct tm &now_local) {
  std::string day = day_label(start_local, now_local);
  std::string at = golf_tee_label(start_local);
  if (day.empty() || at.empty())
    return "";
  return day + " " + at;
}

static bool racing_has_board(const GameSnapshot &s) {
  return s.valid && !s.event_id.empty() && !s.race.rows.empty() &&
         (s.race.phase == RacePhase::LIVE || s.race.phase == RacePhase::BREAK || s.race.phase == RacePhase::FINAL);
}

BoardLines racing_lines(const GameSnapshot &s, bool wide) {
  BoardLines l;
  l.value_narrow = false;  // one panel: no car numbers, the names get their room
  l.status = racing_state_text(s);
  l.status_color = racing_state_color(s);
  if (wide && l.status.size() + 1 < kBoardTitleRow)
    l.title = racing_event_name(s.game_name, kBoardTitleRow - 1 - l.status.size());
  if (!racing_has_board(s))
    return l;  // the card (racing_card_text)
  for (size_t i = 0; i < s.race.rows.size() && i < kGolfBoardRows; i++) {
    const RaceRow &r = s.race.rows[i];
    l.pos.push_back(racing_pos_text(r));
    l.name.push_back(racing_name_fit(r.name, wide ? 11 : 10));
    l.value.push_back(wide ? cut(r.num, 3) : std::string());
    l.detail.push_back(wide ? racing_detail_text(r) : std::string());
    l.value_color.push_back((int) kRaceGray);
    l.name_color.push_back((int) (r.winner ? kRaceGold : kRaceWhite));
  }
  return l;
}

std::string racing_card_text(const GameSnapshot &s, bool wide, const std::string &when) {
  // One line of spleen_12 on a wide board, two of spleen_8 on one panel
  const size_t line = wide ? 21 : 12;
  std::string out = fit_words(racing_event_name(s.game_name, wide ? 21 : 24), line);
  std::string second;
  if (s.event_id.empty())
    second = "";  // the series' name while the first read runs
  else if (s.race.phase == RacePhase::OFF)
    second = off_word(s);
  else if (s.race.phase == RacePhase::PRE)
    second = s.race.next_start > 0 && !when.empty()
                 ? racing_session_label(s.race.next_kind, s.race.next_number) + " " + when
                 : when;  // the calendar's dates
  else
    second = racing_state_text(s);  // on track with nobody listed yet
  if (!second.empty())
    out += "\n" + fit_words(second, line);
  return out;
}

// ---- ticker and Home Assistant ---------------------------------------------------

static const RaceRow *winner_row(const GameSnapshot &s) {
  if (s.race.phase != RacePhase::FINAL)
    return nullptr;
  for (const auto &r : s.race.rows)
    if (r.winner)
      return &r;
  return nullptr;
}

// "1 Antonelli 1:10.763", "8 Wallace +1 lap", "21 Colapinto DNF", "2 Elliott"
static std::string ticker_rows(const GameSnapshot &s, size_t from) {
  std::string out;
  for (size_t i = from; i < s.race.rows.size() && i < kRaceRows; i++) {
    const RaceRow &r = s.race.rows[i];
    if (!out.empty())
      out += ", ";
    out += (r.place > 0 ? std::to_string(r.place) : std::string("-")) + " " + r.name;
    std::string d = racing_detail_long(r);
    if (!d.empty())
      out += " " + d;
  }
  return out;
}

// "Practice 3, Today 2:30p"
static std::string next_session(const GameSnapshot &s, const std::string &when) {
  if (s.race.next_start <= 0)
    return "";
  std::string out = racing_session_long(s.race.next_kind, s.race.next_number);
  return when.empty() ? out : out + ", " + when;
}

static void add_next_event(std::string &out, const GameSnapshot &s, const std::string &when) {
  if (s.race.next.valid)
    add_part(out, "Next: " + s.race.next.name + (when.empty() ? "" : ", " + when));
}

std::string racing_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &when) {
  if (!s.valid || s.state == GameState::NOT_FOUND)
    return "No upcoming race";
  const Racing &r = s.race;
  std::string out = s.game_name;
  switch (r.phase) {
    case RacePhase::PRE: {
      std::string nx = next_session(s, "");
      add_part(out, nx.empty() ? when : when.empty() ? nx : nx + " " + when);
      add_part(out, s.venue);
      if (o.odds)
        add_part(out, s.tv);
      return out;
    }
    case RacePhase::LIVE:
      add_part(out, racing_state_long(s));
      add_part(out, ticker_rows(s, 0));
      if (o.odds)
        add_part(out, s.tv);
      return out;
    case RacePhase::BREAK: {
      add_part(out, racing_state_long(s));
      add_part(out, ticker_rows(s, 0));
      std::string nx = next_session(s, when);
      if (!nx.empty())
        add_part(out, "Next: " + nx);
      if (o.odds)
        add_part(out, s.tv);
      return out;
    }
    case RacePhase::FINAL: {
      add_part(out, "Final");
      const RaceRow *w = winner_row(s);
      if (w != nullptr)
        add_part(out, std::string(w->name) + " wins");
      add_part(out, ticker_rows(s, w != nullptr && w == r.rows.data() ? 1 : 0));
      add_next_event(out, s, when);
      return out;
    }
    case RacePhase::OFF:
      add_part(out, off_word(s));
      add_next_event(out, s, when);
      return out;
  }
  return out;
}

std::string racing_ha_status_text(const GameSnapshot &s, const std::string &when) {
  if (!s.valid || s.state == GameState::NOT_FOUND)
    return "No upcoming race";
  const Racing &r = s.race;
  std::string out = s.game_name;
  switch (r.phase) {
    case RacePhase::PRE: {
      std::string nx = next_session(s, "");
      add_part(out, nx.empty() ? when : when.empty() ? nx : nx + " " + when);
      return out;
    }
    case RacePhase::LIVE:
      add_part(out, racing_session_long(r.kind, r.number));
      if (!r.rows.empty())
        add_part(out, std::string("Leader: ") + r.rows[0].name);
      return out;
    case RacePhase::BREAK:
      add_part(out, racing_state_long(s));
      if (!r.rows.empty())
        add_part(out, std::string(r.kind == RaceKind::SPRINT ? "Winner: " : "Fastest: ") + r.rows[0].name);
      return out;
    case RacePhase::FINAL: {
      add_part(out, "Final");
      const RaceRow *w = winner_row(s);
      if (w != nullptr)
        add_part(out, std::string(w->name) + " wins");
      return out;
    }
    case RacePhase::OFF:
      add_part(out, off_word(s));
      return out;
  }
  return out;
}

Splash racing_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool wide) {
  if (!detail::same_game(prev, cur) || prev.sport != Sport::RACING || cur.sport != Sport::RACING)
    return Splash{};
  if (prev.race.phase != RacePhase::LIVE && prev.race.phase != RacePhase::BREAK)
    return Splash{};
  if (cur.race.kind != RaceKind::RACE)
    return Splash{};  // the race only: never practice, qualifying or a sprint
  const RaceRow *w = winner_row(cur);
  if (w == nullptr || w->name[0] == '\0')
    return Splash{};
  std::string name = no_suffix(w->name);
  for (char &c : name)
    c = (char) toupper((unsigned char) c);
  // spleen_16 on a wide board, spleen_12 on one panel
  name = fit_words(name, wide ? 16 : 10);
  trim_right(name);
  return Splash{name + " WINS!", kRaceGold};
}

// ---- timing ---------------------------------------------------------------------

GameSnapshot racing_next_snapshot(const GolfNext &n) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.sport = Sport::RACING;
  g.event_id = n.event_id;
  g.game_name = n.name;
  g.kickoff_epoch = n.start_epoch;
  g.race.race_epoch = n.end_epoch;
  g.race.phase = RacePhase::PRE;
  g.race.status = GolfStatus::SCHEDULED;
  g.race.kind = RaceKind::RACE;
  return g;
}

// Where a session is: before it, on track, or over.
static int phase_rank(RacePhase p) {
  switch (p) {
    case RacePhase::PRE:
      return 0;
    case RacePhase::LIVE:
      return 1;
    default:
      return 2;
  }
}

bool racing_read_is_older(const GameSnapshot &shown, const GameSnapshot &read) {
  if (!shown.valid || !read.valid || shown.sport != Sport::RACING || read.sport != Sport::RACING ||
      shown.event_id.empty())
    return false;
  // Another weekend is older when it started days before the one shown (an
  // edge's copy of last week's). Not hours: the calendar's card and the
  // header start the same weekend apart.
  if (shown.event_id != read.event_id)
    return read.kickoff_epoch + 2 * kRaceDay <= shown.kickoff_epoch;
  // The same weekend: an earlier session, the same one less far along, or
  // fewer laps into it.
  if (read.race.session_start != shown.race.session_start)
    return read.race.session_start < shown.race.session_start;
  int a = phase_rank(read.race.phase), b = phase_rank(shown.race.phase);
  if (a != b)
    return a < b;
  return read.period < shown.period;
}

uint32_t racing_poll_ms(const GameSnapshot &g, int64_t now) {
  const uint32_t minute = 60 * 1000;
  const Racing &r = g.race;
  switch (r.phase) {
    case RacePhase::LIVE:
      // Stopped: every 2 minutes for the restart, every 15 once it has been
      // held six hours past its start (a race put off overnight)
      if (held(g))
        return held_long(g, now) ? 15 * minute : 2 * minute;
      // A header is at most ~95 KB: every 30 seconds while cars race
      return race_like(r.kind) ? minute / 2 : minute;
    case RacePhase::PRE:
    case RacePhase::BREAK: {
      int64_t t = r.next_start > 0 ? r.next_start : g.kickoff_epoch;
      if (t <= 0)
        return 30 * minute;
      int64_t until = t - now;
      if (until <= -6 * kRaceHour)
        return 15 * minute;  // ESPN never started it (or never says so): look now and then
      if (until <= 10 * 60)
        return minute;  // about to start, or due and not on yet
      if (until <= 2 * kRaceHour)
        return 5 * minute;
      if (r.phase == RacePhase::PRE && until > kRaceSeasonGap)
        return 360 * minute;
      return 30 * minute;
    }
    default:
      // Waiting for ESPN to move its header on to the next race; between
      // seasons (the off-season clock up), a few times a day.
      return racing_off_season(g, now) || now - r.race_epoch > kRaceSeasonGap ? 360 * minute : 30 * minute;
  }
}

bool racing_on_track(const GameSnapshot &g, int64_t now) {
  return g.valid && g.sport == Sport::RACING && g.race.phase == RacePhase::LIVE && !held_long(g, now);
}

bool racing_off_season(const GameSnapshot &g, int64_t now) {
  if (!g.valid || g.sport != Sport::RACING || g.event_id.empty())
    return false;
  const Racing &r = g.race;
  if (r.phase == RacePhase::PRE)
    return g.kickoff_epoch - now > kRaceSeasonGap;
  if (r.phase != RacePhase::FINAL && r.phase != RacePhase::OFF)
    return false;
  if (now - r.race_epoch <= kRaceDay)
    return false;  // the final stays up a day
  if (!r.next.valid)
    return !r.next_unknown;  // none ahead; or the calendar failed, and the final stays up
  return r.next.start_epoch - now > kRaceSeasonGap;
}

bool racing_show_next(const GameSnapshot &g, int64_t now) {
  return g.valid && g.sport == Sport::RACING && g.race.next.valid &&
         (g.race.phase == RacePhase::FINAL || g.race.phase == RacePhase::OFF) && now - g.race.race_epoch > kRaceDay;
}

}  // namespace espn
