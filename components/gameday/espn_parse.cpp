#include "espn_parse.h"
#include "alerts.h"
#include "play_anim.h"
#include "source_hockeytech.h"
#include "source_mlbstats.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>

namespace espn {

// A pointer to a literal: no copy in RAM, and one atomic store when a bench
// build swaps it under a read on the worker task.
static std::atomic<const char *> site_root{"https://site.api.espn.com/apis/site/v2/sports/"};

void set_site_root(const char *root) { site_root.store(root, std::memory_order_relaxed); }

// "https://.../sports/football/nfl", with no trailing slash.
static std::string site_base(League league) {
  const LeagueInfo *l = league_info(league);
  return std::string(site_root.load(std::memory_order_relaxed)) + (l != nullptr ? l->path : "football/nfl");
}

GameSnapshot::GameSnapshot() = default;
GameSnapshot::GameSnapshot(const GameSnapshot &) = default;
GameSnapshot::GameSnapshot(GameSnapshot &&) noexcept = default;
GameSnapshot &GameSnapshot::operator=(const GameSnapshot &) = default;
GameSnapshot &GameSnapshot::operator=(GameSnapshot &&) noexcept = default;
GameSnapshot::~GameSnapshot() = default;

const char *league_path(League league) {
  const LeagueInfo *l = league_info(league);
  if (l == nullptr)
    return "";
  const char *slash = strrchr(l->path, '/');
  return slash != nullptr ? slash + 1 : l->path;
}

std::string team_option(const Team &t) {
  const LeagueInfo *l = league_info(t.league);
  return std::string(l != nullptr ? l->prefix : "?") + ": " + t.name;
}

std::string team_url(League league, uint32_t espn_id) {
  return site_base(league) + "/teams/" + std::to_string(espn_id);
}

std::string schedule_url(League league, uint32_t espn_id) {
  // A soccer team's schedule lists past results unless asked for fixtures.
  if (league_sport(league) == Sport::SOCCER)
    return team_url(league, espn_id) + "/schedule?fixture=true";
  return team_url(league, espn_id) + "/schedule";
}

using detail::civil_from_epoch;

std::string team_day_url(League league, uint32_t group, int64_t day_epoch) {
  return team_day_url(league, group, day_epoch, nullptr);
}

static std::string comp_base(League league, const char *comp_slug);

std::string team_day_url(League league, uint32_t group, int64_t day_epoch, const char *comp_slug) {
  std::string url = comp_base(league, comp_slug) + "/scoreboard?";
  int y, m, d;
  civil_from_epoch(day_epoch - 5 * 3600, y, m, d);
  char buf[48];
  if ((league == League::MCBB || league == League::NCAA) && group != 0) {
    snprintf(buf, sizeof(buf), "groups=%u&", (unsigned) group);
    url += buf;
  }
  snprintf(buf, sizeof(buf), "dates=%04d%02d%02d", y, m, d);
  return url + buf;
}

std::string event_url(League league, const std::string &event_id) {
  return site_base(league) + "/scoreboard/" + event_id;
}

std::string summary_url(League league, const std::string &event_id) {
  return site_base(league) + "/summary?event=" + event_id;
}

// A cup lives next to its league: soccer/eng.1 -> soccer/uefa.champions.
static std::string comp_base(League league, const char *comp_slug) {
  const LeagueInfo *l = league_info(league);
  if (comp_slug == nullptr || l == nullptr)
    return site_base(league);
  const char *slash = strchr(l->path, '/');
  std::string sport = slash != nullptr ? std::string(l->path, slash - l->path + 1) : std::string();
  return site_root.load(std::memory_order_relaxed) + sport + comp_slug;
}

std::string team_url(League league, uint32_t espn_id, const char *comp_slug) {
  return comp_base(league, comp_slug) + "/teams/" + std::to_string(espn_id);
}

std::string event_url(League league, const std::string &event_id, const char *comp_slug) {
  return comp_base(league, comp_slug) + "/scoreboard/" + event_id;
}

// Number of days since 1970-01-01 for a UTC epoch, then split into y/m/d.
// Avoids gmtime_r so the host and device agree regardless of libc quirks.
void detail::civil_from_epoch(int64_t epoch, int &y, int &m, int &d) {
  int64_t z = epoch / 86400 + 719468;
  if (epoch < 0 && epoch % 86400 != 0)
    z -= 1;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  d = (int) (doy - (153 * mp + 2) / 5 + 1);
  m = (int) (mp < 10 ? mp + 3 : mp - 9);
  y = (int) (yy + (m <= 2 ? 1 : 0));
}

std::string scoreboard_url(League league, uint32_t group, int64_t kickoff_epoch, bool time_tbd) {
  std::string url = site_base(league) + "/scoreboard";
  int y, m, d;
  // ESPN's date parameter is in US Eastern; shift the UTC kickoff by five
  // hours so late east-coast games stay on the right day. Being off by an
  // hour around a DST change only matters for a game kicking off between
  // 11 PM and midnight Eastern, which does not happen in football. A TBD
  // game's midnight placeholder is exactly that case, so it goes by noon.
  if (time_tbd)
    kickoff_epoch = tbd_day_epoch(kickoff_epoch);
  civil_from_epoch(kickoff_epoch - 5 * 3600, y, m, d);
  char buf[64];
  if (league == League::NCAA)
    snprintf(buf, sizeof(buf), "?groups=%u&dates=%04d%02d%02d", (unsigned) group, y, m, d);
  else
    snprintf(buf, sizeof(buf), "?dates=%04d%02d%02d", y, m, d);
  url += buf;
  return url;
}

std::string header_url(League league) {
  // The header lives beside the site API, not under it: keep the root's
  // scheme and host (a bench root has no "/apis/").
  std::string root = site_root.load(std::memory_order_relaxed);
  size_t at = root.find("/apis/");
  if (at == std::string::npos) {
    size_t host = root.find("://");
    at = root.find('/', host == std::string::npos ? 0 : host + 3);
  }
  if (at != std::string::npos)
    root.erase(at);
  // The sport is the league path's first part: "golf/pga" is sport=golf.
  const LeagueInfo *l = league_info(league);
  const char *path = l != nullptr ? l->path : "golf/pga";
  const char *slash = strchr(path, '/');
  root += "/apis/v2/scoreboard/header?sport=";
  root.append(path, slash != nullptr ? (size_t) (slash - path) : strlen(path));
  root += "&league=";
  root += league_path(league);
  return root;
}

std::string golf_calendar_url(League league, int64_t now_epoch, bool next_season) {
  // The Eastern date, as every ESPN dates= parameter reads it.
  const int64_t eastern = now_epoch - 5 * 3600;
  int64_t days = eastern / 86400 - (eastern % 86400 < 0 ? 1 : 0);
  int y, m, d;
  char buf[32];
  if (next_season) {
    civil_from_epoch(eastern, y, m, d);
    snprintf(buf, sizeof(buf), "?dates=%04d", y + 1);
  } else {
    // 1970-01-01 was a Thursday: (days + 4) % 7 is the weekday, Sunday 0.
    int wd = (int) (((days + 4) % 7 + 7) % 7);
    days += (2 - wd + 7) % 7;  // on to Tuesday
    civil_from_epoch(days * 86400, y, m, d);
    snprintf(buf, sizeof(buf), "?dates=%04d%02d%02d", y, m, d);
  }
  return site_base(league) + "/scoreboard" + buf;
}

std::string scan_url(League league, int64_t now_epoch) {
  std::string url = site_base(league) + "/scoreboard";
  int y, m, d;
  civil_from_epoch(now_epoch - scan_day_shift(league), y, m, d);
  char buf[64];
  // 80 is every FBS game. College basketball without a group lists only the
  // ranked teams' games (17 of 132 on 2/14/2026); 50 is all of Division I.
  if (league == League::NCAA)
    snprintf(buf, sizeof(buf), "?groups=80&limit=300&dates=%04d%02d%02d", y, m, d);
  else if (league == League::MCBB)
    snprintf(buf, sizeof(buf), "?groups=50&limit=300&dates=%04d%02d%02d", y, m, d);
  else
    snprintf(buf, sizeof(buf), "?dates=%04d%02d%02d", y, m, d);
  url += buf;
  return url;
}

// Dark-background variant, shrunk to 64px by ESPN's image resizer. A 500px
// logo PNG is up to 95KB and takes the ESP32 almost two seconds to decode;
// the 64px one is about 3KB and decodes in a few milliseconds.
std::string dark_logo(const std::string &url) {
  if (url.empty())
    return url;
  std::string out = url;
  size_t pos = out.find("/500/");
  if (pos != std::string::npos)
    out.replace(pos, 5, "/500-dark/");
  size_t path = out.find("/i/teamlogos/");
  if (path == std::string::npos)
    return out;
  return "https://a.espncdn.com/combiner/i?img=" + out.substr(path) + "&w=64&h=64";
}

// The same URL the scoreboard gives this team, so the logo cache downloads it
// once. The scoreboard's NFL, MLB, NBA and NHL logos sit under /scoreboard/,
// and that variant is not always the same picture (the Jets').
std::string team_logo_url(League league, uint32_t espn_id, const char *abbr) {
  std::string a = abbr ? abbr : "";
  for (auto &c : a)
    c = (char) tolower((unsigned char) c);
  const LeagueInfo *l = league_info(league);
  if (l == nullptr || is_board_sport(l->sport))
    return "";  // a golf tour's or a racing series' board has no logo
  if (l->source == Source::MLB_STATS)
    return mlb_logo_url(espn_id);  // the same URL the game read gives the team
  if (l->source == Source::HOCKEYTECH)
    return ht_logo_url(league, espn_id);  // likewise; "" for a team drawn as a badge
  std::string file = l->logo_by_id ? std::to_string(espn_id) : a;
  bool scoreboard = league == League::NFL || league == League::MLB || league == League::NBA || league == League::NHL;
  return dark_logo(std::string("https://a.espncdn.com/i/teamlogos/") + l->logo_dir +
                   (scoreboard ? "/500/scoreboard/" : "/500/") + file + ".png");
}

GameSnapshot team_placeholder(const Team &t) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  if (is_leaderboard(t.league)) {
    // The card with the tour's or the series' name and nothing under it
    g.sport = league_sport(t.league);
    g.game_name = t.name;
    return g;
  }
  g.team_abbr = t.abbr;
  g.team_id = t.espn_id;
  g.team_logo = team_logo_url(t.league, t.espn_id, t.abbr);
  return g;
}

// Every game read from ESPN has an event id; a final card is POST.
bool is_team_placeholder(const GameSnapshot &g) {
  return g.valid && g.state == GameState::PRE && g.event_id.empty();
}

GameSnapshot final_snapshot(const Final &f) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::POST;
  g.completed = true;
  g.kickoff_epoch = f.kickoff_epoch;
  g.short_detail = f.detail;
  g.team_id = f.away_id;
  g.opp_id = f.home_id;
  g.team_score = f.away_score;
  g.opp_score = f.home_score;
  g.team_abbr = f.away_abbr;
  g.opp_abbr = f.home_abbr;
  g.team_color = f.away_color;
  g.opp_color = f.home_color;
  g.team_record = f.away_record;
  g.opp_record = f.home_record;
  g.team_rank = f.away_rank;
  g.opp_rank = f.home_rank;
  g.team_logo = team_logo_url((League) f.league, f.away_id, f.away_abbr.c_str());
  g.opp_logo = team_logo_url((League) f.league, f.home_id, f.home_abbr.c_str());
  g.sport = league_sport((League) f.league);
  return g;
}

GameSnapshot pending_snapshot(const Pending &p) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.event_id = p.event_id;
  g.kickoff_epoch = p.kickoff_epoch;
  g.team_id = p.away_id;
  g.opp_id = p.home_id;
  g.team_abbr = p.away_abbr;
  g.opp_abbr = p.home_abbr;
  g.team_color = p.away_color;
  g.opp_color = p.home_color;
  g.team_record = p.away_record;
  g.opp_record = p.home_record;
  g.team_rank = p.away_rank;
  g.opp_rank = p.home_rank;
  g.tv = p.tv;
  g.team_timeouts = g.opp_timeouts = 3;  // before kickoff; a final card's stay unknown (-1)
  g.to_guess = 3;
  g.team_logo = team_logo_url((League) p.league, p.away_id, p.away_abbr.c_str());
  g.opp_logo = team_logo_url((League) p.league, p.home_id, p.home_abbr.c_str());
  g.sport = league_sport((League) p.league);
  return g;
}

bool parse_team_str(const std::string &json, Schedule &out) { return parse_team(json, out); }

bool parse_upcoming_str(const std::string &json, uint32_t our_team_id, size_t max, std::vector<Upcoming> &out,
                        bool ranks) {
  return parse_upcoming(json, our_team_id, max, out, ranks);
}

bool parse_scoreboard_str(const std::string &json, const std::string &event_id, uint32_t our_team_id,
                          GameSnapshot &out) {
  return parse_scoreboard(json, event_id, our_team_id, out);
}

bool parse_team_game_str(const std::string &json, uint32_t our_team_id, Schedule &out, const std::string &skip_event) {
  return parse_team_game(json, our_team_id, skip_event, out) == 1;
}

bool parse_event_str(const std::string &json, Sport sport, uint32_t our_team_id, GameSnapshot &out,
                     const std::string &event_id, const PlayerWatch *players) {
  return parse_event(json, sport, our_team_id, out, event_id, players);
}

uint32_t parse_color(const std::string &hex) {
  if (hex.size() != 6)
    return 0xFFFFFF;
  char *end = nullptr;
  unsigned long v = strtoul(hex.c_str(), &end, 16);
  if (end == nullptr || *end != '\0')
    return 0xFFFFFF;
  return (uint32_t) v;
}

// ---- leaderboard sports ------------------------------------------------------

GolfNext &board_next(GameSnapshot &g) { return g.sport == Sport::RACING ? g.race.next : g.golf.next; }

bool &board_next_unknown(GameSnapshot &g) {
  return g.sport == Sport::RACING ? g.race.next_unknown : g.golf.next_unknown;
}

GameSnapshot board_next_snapshot(Sport sport, const GolfNext &n) {
  return sport == Sport::RACING ? racing_next_snapshot(n) : golf_next_snapshot(n);
}

bool board_show_next(const GameSnapshot &g, int64_t now) {
  return g.sport == Sport::RACING ? racing_show_next(g, now) : golf_show_next(g, now);
}

bool board_read_is_older(const GameSnapshot &shown, const GameSnapshot &read) {
  return read.sport == Sport::RACING ? racing_read_is_older(shown, read) : golf_read_is_older(shown, read);
}

Splash board_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool wide) {
  return cur.sport == Sport::RACING ? racing_splash(prev, cur, wide) : golf_splash(prev, cur, wide);
}

uint32_t board_poll_ms(const GameSnapshot &g, int64_t now) {
  return g.sport == Sport::RACING ? racing_poll_ms(g, now) : golf_poll_ms(g, now);
}

bool board_off_season(const GameSnapshot &g, int64_t now) {
  return g.sport == Sport::RACING ? racing_off_season(g, now) : golf_off_season(g, now);
}

BoardLines board_lines(const GameSnapshot &g, bool wide) {
  return g.sport == Sport::RACING ? racing_lines(g, wide) : golf_lines(g, wide);
}

std::string board_card_text(const GameSnapshot &g, bool wide, const std::string &when) {
  return g.sport == Sport::RACING ? racing_card_text(g, wide, when) : golf_card_text(g, wide, when);
}

std::string board_event_name(Sport sport, const std::string &name, size_t max) {
  return sport == Sport::RACING ? racing_event_name(name, max) : golf_event_name(name, max);
}

Splash decide_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral,
                     uint32_t alerts, bool wide) {
  if (cur.sport == Sport::GOLF)
    return golf_splash(prev, cur, false);  // the one-panel splash fits both widths
  if (cur.sport == Sport::RACING)
    return racing_splash(prev, cur, false);
  Splash win;
  if (detail::same_game(prev, cur) && detail::went_final(prev, cur)) {
    // A shootout decides a level soccer game.
    int us = cur.team_score, them = cur.opp_score;
    if (cur.sport == Sport::SOCCER && us == them && cur.soc.team_shootout >= 0 && cur.soc.opp_shootout >= 0) {
      us = cur.soc.team_shootout;
      them = cur.soc.opp_shootout;
    }
    win = detail::win_splash(cur, neutral, us, them);
  }
  // A game-ending play outranks the win splash.
  const BigPlay p = detect_big_play(prev, cur, alerts, opponent_splashes);
  if (p.type != Alert::NONE &&
      (win.text.empty() || p.type == Alert::WALK_OFF || p.type == Alert::BUZZER_BEATER)) {
    Splash s = big_play_splash(p, cur, neutral, wide);
    splash_anim(s, p.type, cur);
    return s;
  }
  return win;
}

namespace detail {

uint8_t rank_of(JsonObjectConst c) {
  int r = c["curatedRank"]["current"] | 99;
  return r >= 1 && r < 99 ? (uint8_t) r : 0;
}

void add_part(std::string &out, const std::string &part) {
  if (part.empty())
    return;
  if (!out.empty())
    out += " | ";
  out += part;
}

// "#5 OSU 9-1", "DAL Seed 4"; "" with neither.
static std::string side_line(uint8_t rank, const std::string &abbr, const std::string &rec) {
  if (rec.empty() && rank == 0)
    return "";
  std::string out = rank_label(rank, abbr, SIZE_MAX);
  if (!rec.empty())
    out += " " + rec;
  return out;
}

std::string records_line(const GameSnapshot &s, const std::string &standing) {
  std::string out;
  add_part(out, side_line(s.team_rank, s.team_abbr, standing.empty() ? s.team_record : standing));
  add_part(out, side_line(s.opp_rank, s.opp_abbr, s.opp_record));
  return out;
}

std::string ranked_matchup(const GameSnapshot &s) {
  if (s.team_rank == 0 && s.opp_rank == 0)
    return "";
  return rank_label(s.team_rank, s.team_abbr, SIZE_MAX) + " vs " + rank_label(s.opp_rank, s.opp_abbr, SIZE_MAX);
}

void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o, const std::string &odds) {
  if (!o.odds)
    return;
  add_part(out, odds);
  if (!s.over_under.empty())
    add_part(out, "O/U " + s.over_under);
  add_part(out, s.tv);
}

void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o) { add_odds(out, s, o, s.odds); }

void add_last_play(std::string &out, std::string play) {
  if (play.size() > 160)
    play = play.substr(0, 157) + "...";
  add_part(out, play);
}

bool same_game(const GameSnapshot &prev, const GameSnapshot &cur) {
  return prev.valid && cur.valid && prev.event_id == cur.event_id;
}

Splash win_splash(const GameSnapshot &cur, bool neutral, int us, int them) {
  // ESPN also moves a suspended or canceled game to post, with completed
  // false. Only a completed game has a winner.
  if (!cur.completed)
    return Splash{};
  auto win = [](const std::string &abbr, const std::string &color) {
    return Splash{abbr + " WINS!", parse_color(color), abbr, false, (uint8_t) (abbr.size() < 255 ? abbr.size() + 1 : 0),
                  ""};
  };
  if (us > them)
    return win(cur.team_abbr, cur.team_color);
  if (neutral && them > us)
    return win(cur.opp_abbr, cur.opp_color);
  return Splash{};
}

void fill_game_filter(JsonObject ev, Sport sport) {
  fill_event_filter(ev);
  if (sport == Sport::FOOTBALL) {
    // ESPN sends end.team even when possession is missing.
    JsonObject lp = ev["competitions"][0]["situation"]["lastPlay"];
    lp["type"]["text"] = true;
    lp["id"] = true;
    lp["end"]["team"]["id"] = true;
    lp["team"]["id"] = true;
  }
  if (sport == Sport::BASEBALL)
    fill_baseball_filter(ev);
  if (sport == Sport::SOCCER)
    fill_soccer_filter(ev);
  if (sport == Sport::BASKETBALL)
    fill_basketball_filter(ev);
  if (sport == Sport::HOCKEY)
    fill_hockey_filter(ev);
}

bool game_from_event(JsonObjectConst ev, Sport sport, uint32_t our_team_id, GameSnapshot &out) {
  GameSnapshot s;
  if (!snapshot_from_event(ev, our_team_id, s))
    return false;
  s.sport = sport;
  if (sport == Sport::BASEBALL)
    baseball_from_event(ev, s);
  if (sport == Sport::SOCCER)
    soccer_from_event(ev, s);
  if (sport == Sport::BASKETBALL)
    basketball_from_event(ev, s);
  if (sport == Sport::HOCKEY)
    hockey_from_event(ev, s);
  out = std::move(s);
  return true;
}

DayGame day_game(JsonObjectConst ev) {
  DayGame d;
  d.event_id = str_or_empty(ev["id"]);
  const std::string state = str_or_empty(ev["status"]["type"]["state"]);
  d.state = state == "in"     ? GameState::IN
            : state == "post" ? GameState::POST
            : state == "pre"  ? GameState::PRE
                              : GameState::NOT_FOUND;
  d.kickoff_epoch = parse_iso8601_z(str_or_empty(ev["date"]));
  JsonObjectConst comp = ev["competitions"][0];
  d.time_tbd = time_tbd(comp);
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>())
    (str_or_empty(c["homeAway"]) == "home" ? d.home_id : d.away_id) = (uint32_t) atol(str_or_empty(c["id"]).c_str());
  return d;
}

}  // namespace detail

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::records_line;

std::string no_game_text(bool clock_set, bool live_mode, bool no_game_confirmed, uint32_t misses, bool captive,
                         Source source) {
  if (captive)
    return "Wi-Fi needs a sign-in";
  if (!clock_set)
    return "Getting ready";
  if (live_mode)
    return "";
  if (!no_game_confirmed && misses > 0)
    return source == Source::MLB_STATS    ? "Can't reach MLB"
           : source == Source::HOCKEYTECH ? "Can't reach HockeyTech"
                                          : "Can't reach ESPN";
  return "";
}

std::string rank_text(uint8_t rank) { return rank == 0 ? std::string() : "#" + std::to_string(rank); }

std::string rank_label(uint8_t rank, const std::string &text, size_t max_chars) {
  if (rank == 0 || text.empty())
    return text;
  std::string t = rank_text(rank) + " " + text;
  return t.size() <= max_chars ? t : text;
}

std::string place_short(const std::string &summary) {
  // "1st", "10th" before " in "
  const size_t in = summary.find(" in ");
  if (in == std::string::npos || in == 0 || in > 5 || !isdigit((unsigned char) summary[0]))
    return "";
  std::string group = summary.substr(in + 4);
  size_t at = group.find(" Division");
  if (at != std::string::npos)
    group.erase(at, strlen(" Division"));
  // The initials keep "Conference": "National Football Conference" is "NFC".
  const std::string named = group;
  at = group.find(" Conference");
  if (at != std::string::npos)
    group.erase(at, strlen(" Conference"));
  const size_t sp = group.find(' ');
  if (sp == std::string::npos) {
    static const char *const kShort[][2] = {
        {"Eastern", "East"},       {"Western", "West"}, {"Central", "Cent"},  {"Pacific", "Pac"},  {"Atlantic", "Atl"},
        {"Metropolitan", "Metro"}, {"Southeast", "SE"}, {"Southwest", "SW"}, {"Northwest", "NW"}, {"Northeast", "NE"},
    };
    for (const auto &k : kShort)
      if (group == k[0])
        group = k[1];
  } else {
    // "NFC East" is "NFCE"; else initials, "EPL".
    bool caps = sp > 0 && group.find(' ', sp + 1) == std::string::npos;
    for (size_t i = 0; i < sp; i++)
      caps = caps && isupper((unsigned char) group[i]);
    std::string t = caps ? group.substr(0, sp + 2) : std::string();
    if (caps)
      t.erase(sp, 1);
    for (size_t i = 0; !caps && i < named.size(); i++)
      if (named[i] != ' ' && (i == 0 || named[i - 1] == ' '))
        t += (char) toupper((unsigned char) named[i]);
    group = t;
  }
  std::string place = summary.substr(0, in);
  return group.empty() ? place : place + " " + group;
}

std::string standing_text(Standing kind, uint8_t seed, const std::string &place) {
  if (kind == Standing::SEED && seed > 0)
    return "Seed " + std::to_string(seed);
  return kind == Standing::PLACE ? place : std::string();
}

size_t record_row_chars(int clock_px, int board_px, int row_px, int char_px) {
  if (row_px <= 0 || char_px <= 0)
    return 0;
  size_t n = (size_t) (row_px / char_px);
  // Mirrors LVGL centering, which rounds the leftover down.
  const int clock_left = board_px / 2 - (clock_px + 1) / 2;
  while (clock_px > 0 && n > 0 && (row_px - (int) n * char_px) / 2 + (int) n * char_px >= clock_left)
    n--;
  return n;
}

std::string board_standing(const std::string &standing, size_t room) {
  if (standing.size() <= room)
    return standing;
  const size_t sp = standing.find(' ');
  if (sp == std::string::npos)
    return "";
  std::string t = standing;
  t.erase(sp, 1);
  if (t.size() <= room)
    return t;
  // A seed's "Seed" alone says nothing
  return isdigit((unsigned char) standing[0]) && sp <= room ? standing.substr(0, sp) : std::string();
}

std::string status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local) {
  if (s.sport == Sport::GOLF)
    return golf_status_text(s, o, kickoff_local);
  if (s.sport == Sport::RACING)
    return racing_status_text(s, o, kickoff_local);
  if (s.sport == Sport::BASEBALL)
    return baseball_status_text(s, o, kickoff_local);
  if (s.sport == Sport::SOCCER)
    return soccer_status_text(s, o, kickoff_local);
  if (s.sport == Sport::BASKETBALL)
    return basketball_status_text(s, o, kickoff_local);
  if (s.sport == Sport::HOCKEY)
    return hockey_status_text(s, o, kickoff_local);
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);
      add_part(out, detail::ranked_matchup(s));
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      add_odds(out, s, o);
      if (s.venue != s.game_name)  // "Rose Bowl | ... | Rose Bowl"
        add_part(out, s.venue);
      return out;
    case GameState::POST:
      // A game that ended without being completed says why: Postponed,
      // Canceled or Suspended
      out = s.completed || s.short_detail.empty() ? std::string("Final") : s.short_detail;
      add_part(out, records_line(s, o.team_standing));
      return out;
    case GameState::IN:
      break;
  }
  if (is_halftime(s)) {
    out = "Halftime";
    add_part(out, records_line(s, o.team_standing));
    return out;
  }
  if (o.down_distance && in_red_zone(s))
    add_part(out, "RED ZONE");
  if (o.down_distance && !football_break(s))
    add_part(out, s.down_distance);
  // ESPN leaves stale break words in shortDetail; show the clock.
  const bool words = s.short_detail.find(':') == std::string::npos;
  const std::string clock =
      words && (s.resumed || clock_running(s) || s.short_detail.find("Halftime") != std::string::npos)
          ? clock_text(s)
          : s.short_detail;
  if (o.clock)
    add_part(out, clock);
  if (o.last_play)
    add_last_play(out, s.last_play);
  if (out.empty())
    out = clock.empty() ? "In Progress" : clock;
  return out;
}

std::string ha_status_text(const GameSnapshot &s, const std::string &kickoff_local) {
  if (s.sport == Sport::GOLF)
    return golf_ha_status_text(s, kickoff_local);
  if (s.sport == Sport::RACING)
    return racing_ha_status_text(s, kickoff_local);
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);
      add_part(out, s.team_abbr + " vs " + s.opp_abbr);
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      return out;
    case GameState::POST:
      out = s.completed || s.short_detail.empty() ? std::string("Final") : s.short_detail;
      break;
    case GameState::IN:
      // Football's halftime comes from status and plays, not words.
      out = (s.sport == Sport::FOOTBALL ? is_halftime(s) : s.short_detail.find("Halftime") != std::string::npos)
                ? "Halftime"
                : "Live";
      break;
  }
  add_part(out, s.team_abbr + " " + std::to_string(s.team_score) + ", " + s.opp_abbr + " " +
                    std::to_string(s.opp_score));
  return out;
}

// Bowls named after their sponsor ("Allstate Sugar Bowl") go by the word
// before "Bowl". Only bowls whose sponsor changes or runs long are listed; one
// not here keeps its full name.
static const char *const kBowls[] = {
    "Rose",       "Sugar",        "Orange",     "Cotton",      "Fiesta",          "Peach",        "Citrus",
    "Gator",      "Sun",          "Liberty",    "Alamo",       "Holiday",         "Texas",        "Music City",
    "Las Vegas",  "Pinstripe",    "Military",   "New Mexico",  "Independence",    "Gasparilla",   "Frisco",
    "Fenway",     "Armed Forces", "Arizona",    "Hawai'i",     "Hawaii",          "Cure",         "New Orleans",
    "Camellia",   "Boca Raton",   "Birmingham", "Bahamas",     "First Responder", "Myrtle Beach",
};

static bool replace_first(std::string &s, const char *from, const char *to) {
  size_t at = s.find(from);
  if (at == std::string::npos)
    return false;
  s.replace(at, strlen(from), to);
  return true;
}

// "FLEX: 3:30 - 4:30pm OR 6 - 8pm ET start": ESPN puts a TBD game's TV
// window in the same notes as game names.
static bool has_time_of_day(const std::string &lower) {
  for (size_t i = 0; i + 2 < lower.size(); i++) {
    if (!isdigit((unsigned char) lower[i]))
      continue;
    size_t j = i + 1;
    while (j < lower.size() && lower[j] == ' ')
      j++;
    if (j + 1 < lower.size() && (lower[j] == 'a' || lower[j] == 'p') && lower[j + 1] == 'm')
      return true;
  }
  return lower.find(" et start") != std::string::npos;
}

std::string short_game_name(const std::string &headline) {
  std::string s = headline;
  std::string lower = s;
  for (char &c : lower)
    c = (char) tolower((unsigned char) c);
  if (has_time_of_day(lower))
    return "";
  // Sponsor tails: "... Presented by AT&T", "LA Bowl Hosted by Gronk"
  for (const char *tail : {" presented by ", " powered by ", " hosted by "}) {
    size_t at = lower.find(tail);
    if (at != std::string::npos) {
      s.resize(at);
      lower.resize(at);
    }
  }
  // A playoff game at a bowl goes by the bowl
  size_t at = s.find(" at the ");
  if (at != std::string::npos && s.find(" Bowl", at) != std::string::npos)
    s = s.substr(at + 8);
  if (!replace_first(s, "College Football Playoff National Championship", "CFP Championship"))
    replace_first(s, "College Football Playoff", "CFP");
  // "NFC Wild Card Playoffs", "AFC Divisional Playoffs"
  static const char kPlayoffs[] = " Playoffs";
  size_t n = sizeof(kPlayoffs) - 1;
  if (s.size() > n && s.compare(s.size() - n, n, kPlayoffs) == 0)
    s.resize(s.size() - n);
  for (const char *bowl : kBowls) {
    std::string name = std::string(bowl) + " Bowl";
    size_t b = s.find(name);
    if (b != std::string::npos && (b == 0 || s[b - 1] == ' ')) {
      s = name;
      break;
    }
  }
  return s;
}

static std::string clock_12h(const struct tm &t) {
  int h = t.tm_hour % 12;
  if (h == 0)
    h = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  return buf;
}

// Days since a fixed day for a struct tm's date, from its year and day of the
// year, counting leap years, so Dec 31 and Jan 1 are one day apart. In int64
// for any year a struct tm can hold.
static int64_t tm_day_number(const struct tm &t) {
  int64_t y = (int64_t) t.tm_year + 1899;  // whole years before this one
  auto floor_div = [](int64_t a, int64_t b) { return a / b - (a % b != 0 && a < 0 ? 1 : 0); };
  return y * 365 + floor_div(y, 4) - floor_div(y, 100) + floor_div(y, 400) + t.tm_yday;
}

std::string day_label(const struct tm &kick_local, const struct tm &now_local) {
  static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  // An ESPTime the panel couldn't convert comes out with month and weekday
  // -1: no kickoff time to show.
  if (kick_local.tm_mon < 0 || kick_local.tm_mon > 11 || kick_local.tm_wday < 0 || kick_local.tm_wday > 6)
    return "";
  int64_t diff = tm_day_number(kick_local) - tm_day_number(now_local);
  if (diff == 0)
    return "Today";
  if (diff == 1)
    return "Tomorrow";
  if (diff > 1 && diff < 7)
    return kDays[kick_local.tm_wday];
  char buf[16];
  snprintf(buf, sizeof(buf), "%s %d", kMonths[kick_local.tm_mon], kick_local.tm_mday);
  return buf;
}

std::string kickoff_label(const struct tm &kick_local, const struct tm &now_local, bool time_tbd) {
  std::string when = day_label(kick_local, now_local);
  if (when.empty())
    return "";
  return when + " " + (time_tbd ? std::string("TBD") : clock_12h(kick_local));
}

static int64_t epoch_from_civil(int y, int mo, int d, int h, int mi, int sec) {
  // days from civil (Howard Hinnant's algorithm)
  int yy = y - (mo <= 2 ? 1 : 0);
  int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
  int64_t yoe = yy - era * 400;
  int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = era * 146097 + doe - 719468;
  return days * 86400 + h * 3600 + mi * 60 + sec;
}

// "2026-09-05T19:30Z" or "2026-09-05T19:30:00Z" (ESPN always sends UTC).
// The field widths keep a run of digits from overflowing an int, and a date
// out of range reads as no date, like garbage does.
int64_t parse_iso8601_z(const std::string &s) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
  int n = sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec);
  if (n < 5 || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 ||
      sec < 0 || sec > 60)
    return 0;
  return epoch_from_civil(y, mo, d, h, mi, sec);
}

int64_t parse_http_date(const std::string &s) {
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  // "Sat, 17 Oct 2026 23:39:29 GMT": fixed width, so check it by position.
  if (s.size() != 29 || s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' || s[16] != ' ' ||
      s[19] != ':' || s[22] != ':' || s.compare(25, 4, " GMT") != 0)
    return 0;
  auto num = [&s](size_t at, size_t len) {
    int v = 0;
    for (size_t i = at; i < at + len; i++) {
      if (s[i] < '0' || s[i] > '9')
        return -1;
      v = v * 10 + (s[i] - '0');
    }
    return v;
  };
  int mo = 0;
  for (int i = 0; i < 12; i++)
    if (s.compare(8, 3, kMonths[i]) == 0)
      mo = i + 1;
  int d = num(5, 2), y = num(12, 4), h = num(17, 2), mi = num(20, 2), sec = num(23, 2);
  if (mo == 0 || d < 1 || d > 31 || y < 2020 || y > 2099 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 ||
      sec > 60)
    return 0;
  return epoch_from_civil(y, mo, d, h, mi, sec);
}

static std::string period_name(int period) {
  switch (period) {
    case 1:
      return "1st";
    case 2:
      return "2nd";
    case 3:
      return "3rd";
    case 4:
      return "4th";
    case 5:
      return "OT";
    default:
      return period > 5 ? std::to_string(period - 4) + "OT" : "";
  }
}

bool is_halftime(const GameSnapshot &s) {
  if (s.resumed)
    return false;
  if (s.status_name == "STATUS_HALFTIME")
    return true;
  if (s.short_detail.find("Halftime") == std::string::npos)
    return false;
  return s.status_name.empty() && s.period < 3;
}

// shortDetail says "Halftime" but the second half is under way.
static bool stale_halftime(const GameSnapshot &s) {
  return s.short_detail.find("Halftime") != std::string::npos && !is_halftime(s);
}

bool clock_running(const GameSnapshot &s) {
  return s.status_name == "STATUS_IN_PROGRESS" && s.period > 0 &&
         s.display_clock.find(':') != std::string::npos && s.display_clock != "0:00";
}

std::string play_clock(const std::string &p) {
  // "(M:SS)" or "(MM:SS)" at the start
  size_t close = p.find(')');
  if (p.empty() || p[0] != '(' || close == std::string::npos || close < 5 || close > 6)
    return "";
  std::string c = p.substr(1, close - 1);
  size_t colon = c.find(':');
  if (colon == std::string::npos || colon + 3 != c.size())
    return "";
  for (size_t i = 0; i < c.size(); i++)
    if (i != colon && (c[i] < '0' || c[i] > '9'))
      return "";
  if (colon == 2 && c[0] == '0')
    c.erase(0, 1);  // "00:14" reads "0:14", as ESPN's displayClock does
  return c == "0:00" ? "" : c;
}

// A resumed halftime counts as the 3rd quarter.
static int board_period(const GameSnapshot &s) {
  if (!s.resumed || s.period >= 3)
    return s.period;
  return s.play_secs_left >= 0 && s.play_secs_left < 900 ? 4 : 3;
}

static int timeout_half(int period) { return period <= 2 ? 1 : (period <= 4 ? 2 : period - 2); }

static int play_secs(const std::string &last_play) {
  const std::string c = play_clock(last_play);
  if (c.empty())
    return -1;
  size_t colon = c.find(':');
  return atoi(c.substr(0, colon).c_str()) * 60 + atoi(c.substr(colon + 1).c_str());
}

// By ESPN's status alone, ignoring plays.
static bool break_now(const GameSnapshot &s) {
  if (s.sport != Sport::FOOTBALL || s.state != GameState::IN || s.resumed)
    return false;
  if (is_halftime(s))
    return true;
  // ESPN may end a half as IN_PROGRESS at 0:00.
  const bool ended = s.status_name == "STATUS_END_PERIOD" ||
                     (s.short_detail.rfind("End of", 0) == 0 && s.display_clock == "0:00");
  return ended && (s.period == 2 || s.period >= 4);
}

// ESPN's between-play words, which aren't plays.
static bool break_words(const std::string &p) {
  return p.empty() || p.rfind("End of", 0) == 0 || p.rfind("END ", 0) == 0 || p.find("Halftime") != std::string::npos;
}

static bool kickoff(const std::string &p) {
  // "kickoff", "Kickoff" or "kicks"
  return p.find("ickoff") != std::string::npos || p.find("kicks") != std::string::npos;
}

// A late first-half play can still arrive after halftime.
static bool after_break(const GameSnapshot &s, int break_period, const std::string &break_play, int break_secs) {
  if (break_words(s.last_play) || s.last_play == break_play)
    return false;
  if (break_period != 2)
    return true;
  const int secs = play_secs(s.last_play);
  // 1800 left can be a first-half kickoff return too.
  if (s.play_secs_left >= 0)
    return s.play_secs_left < 1800 ||
           (s.play_secs_left == 1800 && (secs == 900 || (secs < 0 && kickoff(s.last_play))));
  if (secs >= 0)
    return secs > (break_secs >= 0 ? break_secs : 599);
  return kickoff(s.last_play);
}

static bool first_half_play(const GameSnapshot &s) {
  if (s.play_secs_left >= 0) {
    // At 1800, the 3rd quarter's own kickoff isn't.
    const int secs = play_secs(s.last_play);
    return s.play_secs_left > 1800 ||
           (s.play_secs_left == 1800 && secs != 900 && !(secs < 0 && kickoff(s.last_play)));
  }
  return s.last_play.find("End of 2nd") != std::string::npos || s.last_play.find("End of Half") != std::string::npos ||
         s.last_play.find("Halftime") != std::string::npos;
}

void carry_football(const GameSnapshot &prev, GameSnapshot &next) {
  if (next.sport != Sport::FOOTBALL)
    return;
  const bool game = prev.valid && prev.event_id == next.event_id;
  const bool was_held = game && prev.held;
  if (next.status_name == "STATUS_HALFTIME" && next.state == GameState::IN) {
    if (game && prev.resumed && !first_half_play(next))
      next.resumed = true;  // until ESPN's status catches up
    else
      next.resumed = after_break(next, 2, was_held ? prev.break_play : std::string(), was_held ? prev.break_secs : -1);
  }
  next.held = false;
  next.break_period = 0;
  next.break_play.clear();
  next.break_secs = -1;
  auto hold = [&](int period, const std::string &play, int secs) {
    next.held = true;
    next.break_period = period;
    next.break_play = play;
    next.break_secs = secs;
  };
  if (next.state == GameState::IN && !next.resumed) {
    const int secs = play_secs(next.last_play);
    // A late first-half play moves the break's marks.
    const bool late = was_held && !break_words(next.last_play) && next.last_play != prev.break_play && secs >= 0;
    if (was_held && break_now(next)) {
      hold(prev.break_period, late ? next.last_play : prev.break_play, late ? secs : prev.break_secs);
    } else if (was_held && clock_running(next) && next.period == prev.break_period) {
      // ESPN put time back in the period: play goes on
    } else if (was_held && !after_break(next, prev.break_period, prev.break_play, prev.break_secs)) {
      hold(prev.break_period, late ? next.last_play : prev.break_play, late ? secs : prev.break_secs);
    } else if (!was_held && break_now(next)) {
      hold(next.period >= 4 ? next.period : 2, next.last_play, secs);
    } else if (!was_held && board_period(next) >= 3 && board_period(next) <= 4 && first_half_play(next)) {
      hold(2, next.last_play, secs);  // ESPN's plays haven't reached the second half yet
    }
  }

  const int now_p = board_period(next), had_p = board_period(prev);
  const bool same = game && timeout_half(had_p) == timeout_half(now_p);
  const bool resting = next.state == GameState::IN && next.held && !break_now(next);
  // ESPN sends first-half counts until the second half's first play.
  const bool leftovers = resting && next.break_period == 2 && (now_p == 3 || now_p == 4);
  // Overtime starts with the last period's stale counts.
  const bool stale_ot = resting && next.break_period >= 4 && now_p > next.break_period;
  next.to_guess = 0;
  auto fill = [&](int had, uint8_t bit, int &now) {
    if (leftovers) {
      now = 3;
    } else if (stale_ot) {
      now = -1;
      return;
    } else if (now >= 0) {
      return;  // ESPN's own count
    } else if (same && had >= 0 && !(prev.to_guess & bit)) {
      now = had;
      return;  // ESPN's from an earlier poll
    } else if (same && had >= 0 && (had_p == now_p || (prev.state == GameState::PRE && now_p <= 1))) {
      now = had;  // a guess carries only within its period
    } else if (next.state == GameState::PRE) {
      now = 3;
    } else {
      now = -1;
      return;
    }
    next.to_guess |= bit;
  };
  fill(prev.team_timeouts, 1, next.team_timeouts);
  fill(prev.opp_timeouts, 2, next.opp_timeouts);
}

bool in_red_zone(const GameSnapshot &s) {
  return s.sport == Sport::FOOTBALL && s.state == GameState::IN && s.is_red_zone && s.possession != 0 &&
         !s.short_down_distance.empty() && !football_break(s);
}

bool football_break(const GameSnapshot &s) {
  if (s.sport != Sport::FOOTBALL || s.state != GameState::IN || s.resumed)
    return false;
  return break_now(s) || s.held;
}

std::string clock_text(const GameSnapshot &s) {
  if (is_board_sport(s.sport))
    return "";  // the leaderboard has its own status cell
  if (s.sport == Sport::BASEBALL)
    return baseball_clock_text(s);
  if (s.sport == Sport::SOCCER)
    return soccer_clock_text(s);
  if (s.sport == Sport::BASKETBALL)
    return basketball_clock_text(s);
  if (s.sport == Sport::HOCKEY)
    return hockey_clock_text(s);
  bool has_clock = s.display_clock.find(':') != std::string::npos && s.period > 0;
  bool special = s.short_detail.find(':') == std::string::npos;  // Halftime, End of 3rd, Delayed, Final
  if (s.resumed) {
    // ESPN's status sticks at halftime; plays have the clock.
    const std::string c = play_clock(s.last_play);
    return c.empty() ? period_name(board_period(s)) : c + " " + period_name(board_period(s));
  }
  if (special && is_halftime(s))
    return "Halftime";  // the status says so before shortDetail does ("End of 2nd")
  if (special && (stale_halftime(s) || clock_running(s)))
    return has_clock ? s.display_clock + " " + period_name(s.period) : period_name(s.period);
  if (!has_clock || special) {
    std::string t = s.short_detail;
    size_t dash = t.find(" - ");
    if (dash != std::string::npos)
      t.replace(dash, 3, " ");
    return t;
  }
  return s.display_clock + " " + period_name(s.period);
}

const char *state_name(GameState s) {
  switch (s) {
    case GameState::PRE:
      return "PRE";
    case GameState::IN:
      return "IN";
    case GameState::POST:
      return "POST";
    default:
      return "NOT_FOUND";
  }
}

}  // namespace espn
