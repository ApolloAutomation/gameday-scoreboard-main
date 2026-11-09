// Minor league hockey from HockeyTech's feed: URLs, filters and the parse
// into the shared snapshot types (source_hockeytech.h). Pure, no ESPHome
// includes. Built for size, as sport_golf.cpp is: it runs once a poll at
// most. Only GCC has the pragma.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
#include "source_hockeytech.h"

#include "recap.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace espn {

using detail::field;
using detail::keep;
using detail::text;

namespace {

// The leagues' keys and client codes (ht_set_feed), from the YAML.
struct Feed {
  League league;
  const char *key;
  const char *client;
};
Feed g_feeds[] = {{League::AHL, "", ""}, {League::ECHL, "", ""}};

const Feed *feed_of(League league) {
  for (const Feed &f : g_feeds)
    if (f.league == league)
      return &f;
  return nullptr;
}

const char *const kFeed = "https://lscluster.hockeytech.com/feed/index.php?feed=";

// The team read's games around today: one back, five ahead (today's first).
const int kTeamBack = 1, kTeamAhead = 5;
// A game not started this long after its listed start is left over from a
// past day (a status never updated): not the game to show. A game with no
// start time yet sits at midnight Eastern, so its day has to be over.
const int64_t kStaleStart = 18 * 3600, kStaleTbd = 30 * 3600;
const int64_t kEastern = 5 * 3600;

bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

// The feed sends its numbers as strings ("4"); a number passes too.
long num(JsonVariantConst v) {
  if (v.is<long long>())
    return (long) v.as<long long>();
  const char *s = v.as<const char *>();
  return s != nullptr ? strtol(s, nullptr, 10) : 0;
}
long num(JsonVariantConst v, const char *key) { return num(field(v, key)); }
int bounded(long v, int hi) { return v < 0 ? 0 : v > hi ? hi : (int) v; }

// "1st", "2nd", "3rd", "OT", then "2OT", "3OT" for the playoffs' overtimes.
std::string period_word(int p) {
  static const char *const kWords[] = {"", "1st", "2nd", "3rd", "OT"};
  if (p <= 4)
    return kWords[p < 0 ? 0 : p];
  return std::to_string(p - 3) + "OT";
}

// "Final", "Final/OT", "Final/SO", "Final/2OT": the words ESPN's NHL finals
// use, which the ticker and the finals rotation expect.
std::string final_text(bool shootout, int period) {
  if (shootout)
    return "Final/SO";
  if (period >= 4)
    return period == 4 ? "Final/OT" : "Final/" + std::to_string(period - 3) + "OT";
  return "Final";
}

const char *off_word(const std::string &code, const std::string &words) {
  if (code == "9" || has(words, "Suspend"))
    return "Suspended";
  if (has(words, "Cancel"))
    return "Canceled";
  return "Postponed";
}

// "12:34" from the game centre's "00:12:34" or the scorebar's "12:34", ""
// for anything else.
std::string clock_text(const std::string &c) {
  int h = 0, m = 0, s = 0;
  int n = sscanf(c.c_str(), "%3d:%3d:%3d", &h, &m, &s);
  if (n == 2) {
    s = m;
    m = h;
    h = 0;
  } else if (n != 3) {
    return "";
  }
  m += h * 60;
  if (h < 0 || m < 0 || m > 99 || s < 0 || s > 59)
    return "";
  char buf[8];
  snprintf(buf, sizeof(buf), "%d:%02d", m, s);
  return buf;
}

// "10-3-1": wins, regulation losses, and overtime and shootout losses
// together, the way the NHL counts them.
std::string record(long w, long l, long otl, long sol) {
  return std::to_string(bounded(w, 999)) + "-" + std::to_string(bounded(l, 999)) + "-" +
         std::to_string(bounded(otl + sol, 999));
}

// Midnight Eastern of a start's day: the placeholder ESPN uses for a game
// with no time yet, which the kickoff labels and tbd_day_epoch expect.
int64_t tbd_midnight(int64_t kick) {
  int64_t s = kick - kEastern;
  int64_t day = s >= 0 ? s / 86400 : (s - 86399) / 86400;
  return day * 86400 + kEastern;
}

// One side of a game, whichever read it came from.
struct Side {
  uint32_t id{0};
  int score{0};
  std::string abbr, name, record;
};

// The scorebar's flat keys: HomeID, VisitorCode, HomeWins...
const char *const kBarSuffixes[] = {"ID", "Code", "Nickname", "Goals", "Wins", "RegulationLosses", "OTLosses",
                                    "ShootoutLosses"};

std::string bar_key(bool home, const char *suffix) { return std::string(home ? "Home" : "Visitor") + suffix; }

long bar_num(JsonObjectConst g, bool home, const char *suffix) { return num(g[bar_key(home, suffix)]); }

Side bar_side(JsonObjectConst g, bool home) {
  Side s;
  s.id = (uint32_t) bar_num(g, home, "ID");
  s.abbr = detail::str_or_empty(g[bar_key(home, "Code")]);
  s.name = ascii_fold(detail::str_or_empty(g[bar_key(home, "Nickname")]));
  s.score = bounded(bar_num(g, home, "Goals"), 999);
  if (!g[bar_key(home, "Wins")].isNull())
    s.record = record(bar_num(g, home, "Wins"), bar_num(g, home, "RegulationLosses"), bar_num(g, home, "OTLosses"),
                      bar_num(g, home, "ShootoutLosses"));
  return s;
}

int64_t bar_start(JsonObjectConst g, bool &tbd) {
  int64_t kick = ht_iso_epoch(text(g, "GameDateISO8601"));
  tbd = text(g, "TimeTbd") == "1" || text(g, "DateTbd") == "1";
  return tbd && kick > 0 ? tbd_midnight(kick) : kick;
}

// The game centre has a shot or a goal in this period: it was played, so a
// clock at zero is its end, not a clock not set yet.
bool period_played(JsonObjectConst c, int period) {
  const std::string p = std::to_string(period);
  long n = 0;
  for (const char *side : {"home", "visiting"})
    n += num(c["shots_on_goal"][side][p]) + num(c["scoring"][side][p]);
  return n > 0;
}

Side clock_side(JsonObjectConst c, bool home) {
  JsonObjectConst t = c[home ? "home_team" : "visiting_team"];
  Side s;
  s.id = (uint32_t) num(t, "team_id");
  s.abbr = text(t, "team_code");
  s.score = bounded(num(c, home ? "home_goal_count" : "visiting_goal_count"), 999);
  return s;
}

Side day_side(JsonObjectConst g, bool home) {
  const char *p = home ? "home_" : "visiting_";
  Side s;
  s.id = (uint32_t) num(g[std::string(p) + "team"]);
  s.abbr = detail::str_or_empty(g[std::string(p) + "team_code"]);
  s.score = bounded(num(g[std::string(p) + "goal_count"]), 999);
  JsonObjectConst st = g[std::string(p) + "stats"];
  if (!st.isNull())
    s.record = record(num(st, "wins"), num(st, "losses"), num(st, "ot_losses"), num(st, "shootout_losses"));
  return s;
}

// North American zones a game can be in: the standard offset in minutes,
// whether the zone keeps daylight saving time, and whether it stopped
// changing clocks on 1 November 2026. Anything else is Eastern.
struct Zone {
  const char *city;
  int16_t minutes;
  bool dst;
  bool fixed_2026;
};
// British Columbia, Alberta and Manitoba kept summer time from 2 AM on
// 1 November 2026 (local, as if it were UTC): UTC-7, -6 and -5 all year, as
// the feed's own offsets and tzdata 2026f have them. Their days before that
// keep the old rule.
const int64_t kFixedFrom2026 = 1793498400;
const Zone kZones[] = {
    {"St_Johns", -210, true, false},   {"Halifax", -240, true, false},     {"Moncton", -240, true, false},
    {"Chicago", -360, true, false},    {"Winnipeg", -360, true, true},     {"Regina", -360, false, false},
    {"Denver", -420, true, false},     {"Edmonton", -420, true, true},     {"Boise", -420, true, false},
    {"Phoenix", -420, false, false},   {"Los_Angeles", -480, true, false}, {"Vancouver", -480, true, true},
    {"Anchorage", -540, true, false},
};

// A local time (as if it were UTC) inside daylight saving time, as the US and
// Canada keep it: from 2 AM on March's second Sunday to 2 AM on November's
// first.
bool in_dst(int64_t local) {
  int y = 0, m = 0, d = 0;
  detail::civil_from_epoch(local, y, m, d);
  if (m < 3 || m > 11)
    return false;
  if (m > 3 && m < 11)
    return true;
  int weekday = (int) ((local / 86400 + 4) % 7);  // 0 Sunday; 1970-01-01 was a Thursday
  int first = ((weekday - (d - 1)) % 7 + 7) % 7;   // the 1st's weekday
  int sunday = 1 + (7 - first) % 7;                // the first Sunday
  int hour = (int) ((local % 86400) / 3600);
  if (m == 3)
    return d > sunday + 7 || (d == sunday + 7 && hour >= 2);
  return d < sunday || (d == sunday && hour < 2);
}

std::string ymd(int64_t day) {
  int y = 0, m = 0, d = 0;
  detail::civil_from_epoch(day * 86400, y, m, d);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
  return buf;
}

std::string feed_part(League league) {
  const Feed *f = feed_of(league);
  return std::string("&key=") + (f != nullptr ? f->key : "") + "&client_code=" + (f != nullptr ? f->client : "");
}

}  // namespace

void ht_set_feed(League league, const char *key, const char *client_code) {
  for (Feed &f : g_feeds)
    if (f.league == league) {
      f.key = key != nullptr ? key : "";
      f.client = client_code != nullptr ? client_code : "";
    }
}

const char *ht_client(League league) {
  const Feed *f = feed_of(league);
  return f != nullptr ? f->client : "";
}

std::string ht_team_url(League league, uint32_t team_id) {
  return std::string(kFeed) + "modulekit&view=scorebar" + feed_part(league) + "&team_id=" + std::to_string(team_id) +
         "&numberofdaysback=" + std::to_string(kTeamBack) + "&numberofdaysahead=" + std::to_string(kTeamAhead) +
         "&fmt=json&lang=en";
}

std::string ht_game_url(League league, const std::string &game_id) {
  return std::string(kFeed) + "gc" + feed_part(league) + "&game_id=" + game_id + "&lang_code=en&fmt=json&tab=clock";
}

std::string ht_day_url(League league, int64_t day_epoch) {
  int64_t s = day_epoch - scan_day_shift(league);
  int64_t day = s >= 0 ? s / 86400 : (s - 86399) / 86400;
  return std::string(kFeed) + "modulekit&view=gamesbydate" + feed_part(league) + "&fetch_date=" + ymd(day) +
         "&fmt=json&lang=en";
}

std::string ht_logo_url(League league, uint32_t team_id) {
  const char *client = ht_client(league);
  if (*client == '\0' || team_id == 0)
    return "";
  uint32_t group = 0;
  for (const Team &t : kTeams)
    if (t.league == league && t.espn_id == team_id) {
      group = t.group;
      break;
    }
  if (group & kHtNoSmallLogo)
    return "";
  std::string url = std::string("https://assets.leaguestat.com/") + client + "/logos/50x50/" + std::to_string(team_id);
  if ((group & 0xFFFF) != 0)
    url += "_" + std::to_string(group & 0xFFFF);
  return url + ".png";
}

int64_t ht_iso_epoch(const std::string &iso) {
  int64_t t = parse_iso8601_z(iso);
  if (t <= 0 || iso.size() < 25 || (iso[19] != '+' && iso[19] != '-'))
    return t;
  int h = 0, m = 0;
  if (sscanf(iso.c_str() + 20, "%2d:%2d", &h, &m) != 2 || h < 0 || h > 14 || m < 0 || m > 59)
    return t;
  int64_t offset = h * 3600 + m * 60;
  return iso[19] == '-' ? t + offset : t - offset;
}

int64_t ht_local_epoch(const std::string &date, const std::string &time, const std::string &zone) {
  int64_t local = parse_iso8601_z(date + "T" + time);
  if (local <= 0)
    return 0;
  size_t slash = zone.rfind('/');
  const std::string city = slash == std::string::npos ? zone : zone.substr(slash + 1);
  int minutes = -300;
  bool dst = true, fixed = false;
  for (const Zone &z : kZones)
    if (city == z.city) {
      minutes = z.minutes;
      dst = z.dst;
      fixed = z.fixed_2026 && local >= kFixedFrom2026;
      break;
    }
  if (fixed || (dst && in_dst(local)))
    minutes += 60;
  return local - (int64_t) minutes * 60;
}

void ht_fill_from_schedule(GameSnapshot &g, const Schedule &s) {
  if (g.event_id.empty())
    g.event_id = s.event_id;
  if (s.time_tbd && g.state == GameState::PRE && g.event_id == s.event_id) {
    g.time_tbd = true;
    g.kickoff_epoch = s.kickoff_epoch;
  }
  if (g.team_record.empty())
    g.team_record = s.team_record;
  if (g.opp_record.empty() && s.opp_id != 0 && g.opp_id == s.opp_id)
    g.opp_record = s.opp_record;
}

HtState ht_state(const std::string &code, const std::string &words, bool started) {
  switch (atoi(code.c_str())) {
    case 1:
      return HtState::PRE;
    case 2:
      return HtState::LIVE;
    case 3:  // over, not yet made official
    case 4:
      return HtState::FINAL;
    case 8:  // postponed
    case 9:  // suspended
      return HtState::OFF;
    default:
      break;
  }
  if (words.rfind("Final", 0) == 0)
    return HtState::FINAL;
  if (has(words, "Postpon") || has(words, "Cancel") || has(words, "Suspend"))
    return HtState::OFF;
  return started ? HtState::LIVE : HtState::PRE;
}

namespace detail {

void fill_ht_team_filter(JsonDocument &f) {
  JsonObject g = f["SiteKit"]["Scorebar"].add<JsonObject>();
  keep(g, {"ID", "GameDateISO8601", "TimeTbd", "DateTbd", "GameStatus", "GameStatusStringLong"});
  for (bool home : {false, true})
    for (const char *suffix : kBarSuffixes)
      g[bar_key(home, suffix)] = true;
}

void fill_ht_game_filter(JsonDocument &f) {
  f["GC"]["Parameters"]["game_id"] = true;
  JsonObject c = f["GC"]["Clock"].to<JsonObject>();
  keep(c, {"period", "game_clock", "game_date_iso_8601", "started", "status", "progress", "period_name",
           "season_name", "venue", "home_goal_count", "visiting_goal_count"});
  for (const char *side : {"home_team", "visiting_team"})
    keep(c[side].to<JsonObject>(), {"team_id", "team_code"});
  // Goals and shots by period, a few dozen bytes: whether a period was
  // played, and the shootout's "SO" entry
  keep(c, {"scoring", "shots_on_goal"});
}

void fill_ht_day_filter(JsonDocument &f) {
  JsonObject g = f["SiteKit"]["Gamesbydate"].add<JsonObject>();
  keep(g, {"id", "date_played", "schedule_time", "timezone", "time_tbd", "status", "game_status", "started",
           "period", "shootout", "home_team", "visiting_team", "home_team_code", "visiting_team_code",
           "home_goal_count", "visiting_goal_count"});
  for (const char *side : {"home_stats", "visiting_stats"})
    keep(g[side].to<JsonObject>(), {"wins", "losses", "ot_losses", "shootout_losses"});
}

int ht_team_from_doc(JsonObjectConst root, uint32_t our_id, int64_t now, Schedule &out,
                     std::vector<Upcoming> *upcoming, size_t max) {
  JsonArrayConst games = root["SiteKit"]["Scorebar"];
  if (games.isNull())
    return -1;
  Schedule s;
  s.valid = true;
  bool found = false;
  if (upcoming != nullptr)
    upcoming->clear();
  for (JsonObjectConst g : games) {
    const std::string id = text(g, "ID");
    HtState st = ht_state(text(g, "GameStatus"), text(g, "GameStatusStringLong"));
    if (id.empty() || st == HtState::FINAL || st == HtState::OFF)
      continue;
    bool tbd = false;
    int64_t kick = bar_start(g, tbd);
    if (st == HtState::PRE && now > 0 && kick > 0 && now - kick > (tbd ? kStaleTbd : kStaleStart))
      continue;
    Side away = bar_side(g, false), home = bar_side(g, true);
    bool at_home = our_id != 0 && home.id == our_id;
    Side &us = at_home ? home : away, &them = at_home ? away : home;
    if (!found) {
      found = true;
      s.event_id = id;
      s.kickoff_epoch = kick;
      s.time_tbd = tbd;
      s.opp_id = them.id;
      s.opp_abbr = them.abbr;
      s.team_record = us.record;
      s.opp_record = them.record;
    }
    if (upcoming == nullptr)
      break;
    if (st != HtState::PRE || upcoming->size() >= max || them.id == 0)
      continue;
    Upcoming u;
    u.event_id = id;
    u.kickoff_epoch = kick;
    u.time_tbd = tbd;
    u.opp_id = them.id;
    u.opp_abbr = std::move(them.abbr);
    u.opp_name = std::move(them.name);
    u.home = at_home;
    upcoming->push_back(std::move(u));
  }
  out = std::move(s);
  return found ? 1 : 0;
}

bool ht_game_from_doc(JsonObjectConst root, League league, uint32_t our_id, GameSnapshot &out) {
  JsonObjectConst c = root["GC"]["Clock"];
  if (c.isNull())
    return false;
  Side away = clock_side(c, false), home = clock_side(c, true);
  GameSnapshot s;
  s.sport = Sport::HOCKEY;
  s.event_id = text(root["GC"]["Parameters"], "game_id");
  s.kickoff_epoch = ht_iso_epoch(text(c, "game_date_iso_8601"));
  s.venue = ascii_fold(text(c, "venue"));
  s.nhl.playoffs = has(text(c, "season_name"), "Playoff");
  const int period = bounded(num(c, "period"), 20);
  const std::string progress = text(c, "progress"), pname = text(c, "period_name");
  // A shootout decides only a regular-season game. The game centre leaves
  // the period at OT and says so in its words and its scoring.
  const bool shootout = !s.nhl.playoffs && (has(progress, "SO") || has(pname, "SO") || has(pname, "Shootout") ||
                                            !c["scoring"]["home"]["SO"].isNull());
  switch (ht_state(text(c, "status"), progress, text(c, "started") == "1")) {
    case HtState::PRE:
      s.state = GameState::PRE;
      s.short_detail = "Scheduled";
      s.status_name = "STATUS_SCHEDULED";
      break;
    case HtState::LIVE:
      s.state = GameState::IN;
      if (shootout || (!s.nhl.playoffs && period >= 5)) {
        s.period = 5;
        s.short_detail = "Shootout";
        s.status_name = "STATUS_SHOOTOUT";
        break;
      }
      s.period = period > 0 ? period : 1;
      s.display_clock = clock_text(text(c, "game_clock"));
      if (s.display_clock == "0:00" && !period_played(c, s.period)) {
        // Status 2 with the clock not set yet (the pre-game clock is zero):
        // the period alone until it is.
        s.display_clock.clear();
        s.short_detail = period_word(s.period);
        s.status_name = "STATUS_IN_PROGRESS";
      } else if (has(progress, "Intermission") || progress.rfind("End", 0) == 0 || s.display_clock == "0:00") {
        // Between periods the clock sits at zero until the next one starts.
        s.display_clock = "0:00";
        s.short_detail = "End of " + period_word(s.period);
        s.status_name = "STATUS_END_PERIOD";
      } else {
        s.short_detail = s.display_clock.empty() ? "In Progress" : s.display_clock + " - " + period_word(s.period);
        s.status_name = "STATUS_IN_PROGRESS";
      }
      break;
    case HtState::FINAL:
      s.state = GameState::POST;
      s.completed = true;
      s.period = shootout ? 5 : period;
      s.short_detail = final_text(shootout, period);
      s.status_name = "STATUS_FINAL";
      break;
    case HtState::OFF:
      // ESPN's post state without a result
      s.state = GameState::POST;
      s.short_detail = off_word(text(c, "status"), progress);
      s.status_name = "STATUS_POSTPONED";
      break;
  }
  s.team_home = our_id != 0 && home.id == our_id;
  if (!s.team_home && (our_id == 0 || away.id != our_id))
    return false;
  Side &us = s.team_home ? home : away, &them = s.team_home ? away : home;
  s.team_id = us.id;
  s.opp_id = them.id;
  s.team_abbr = std::move(us.abbr);
  s.opp_abbr = std::move(them.abbr);
  s.team_score = us.score;
  s.opp_score = them.score;
  s.team_logo = ht_logo_url(league, us.id);
  s.opp_logo = ht_logo_url(league, them.id);
  s.team_winner = s.completed && s.team_score > s.opp_score;
  s.valid = true;
  out = std::move(s);
  return true;
}

bool ht_day_from_doc(JsonObjectConst root, std::vector<LiveGame> &out, int64_t *next_kickoff,
                     std::vector<Final> *finals, std::vector<Pending> *pending) {
  JsonArrayConst games = root["SiteKit"]["Gamesbydate"];
  if (games.isNull())
    return false;
  if (next_kickoff != nullptr)
    *next_kickoff = 0;
  // Kept apart, so the cap counts this document only.
  std::vector<Pending> later;
  size_t kept = 0;
  for (JsonObjectConst g : games) {
    const std::string id = text(g, "id");
    Side away = day_side(g, false), home = day_side(g, true);
    if (id.empty() || away.id == 0 || home.id == 0)
      continue;
    const bool tbd = text(g, "time_tbd") == "1";
    const int64_t kick = ht_local_epoch(text(g, "date_played"), text(g, "schedule_time"), text(g, "timezone"));
    switch (ht_state(text(g, "status"), text(g, "game_status"), text(g, "started") == "1")) {
      case HtState::PRE: {
        if (tbd || kick <= 0)
          break;
        if (next_kickoff != nullptr && (*next_kickoff == 0 || kick < *next_kickoff))
          *next_kickoff = kick;
        if (pending == nullptr)
          break;
        Pending p;
        p.kickoff_epoch = kick;
        p.event_id = id;
        p.away_id = away.id;
        p.home_id = home.id;
        p.away_abbr = std::move(away.abbr);
        p.home_abbr = std::move(home.abbr);
        p.away_record = std::move(away.record);
        p.home_record = std::move(home.record);
        auto at = std::upper_bound(later.begin(), later.end(), p, pending_before);
        if (later.size() < kPendingKeep || at != later.end()) {
          later.insert(at, std::move(p));
          if (later.size() > kPendingKeep)
            later.pop_back();
        }
        break;
      }
      case HtState::FINAL: {
        if (finals == nullptr || kept >= kRecapCap)
          break;
        kept++;
        const std::string so = text(g, "shootout");
        Final f;
        f.kickoff_epoch = kick;
        f.detail = final_text(!so.empty() && so != "0", bounded(num(g, "period"), 20));
        f.away_id = away.id;
        f.home_id = home.id;
        f.away_score = away.score;
        f.home_score = home.score;
        f.away_abbr = std::move(away.abbr);
        f.home_abbr = std::move(home.abbr);
        f.away_record = std::move(away.record);
        f.home_record = std::move(home.record);
        finals->push_back(std::move(f));
        break;
      }
      case HtState::LIVE: {
        LiveGame lg;
        lg.event_id = id;
        lg.away_id = away.id;
        lg.home_id = home.id;
        lg.away_abbr = std::move(away.abbr);
        lg.home_abbr = std::move(home.abbr);
        out.push_back(std::move(lg));
        break;
      }
      case HtState::OFF:
        break;
    }
  }
  if (pending != nullptr)
    for (auto &p : later)
      pending->push_back(std::move(p));
  return true;
}

}  // namespace detail
}  // namespace espn
