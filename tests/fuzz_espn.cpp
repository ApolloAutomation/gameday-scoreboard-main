// Fuzz target: one input goes through every ESPN parser the panel runs, the
// text built from what they read, and the finals rotation (recap.h). ESPN's
// documents share one shape (events, competitions, competitors), so a
// mutated scoreboard also reaches the event and team parsers. The same input,
// line by line, is also read as the response headers the panel acts on (Date,
// Retry-After) and as the answer to the sign-in probe (captive.h). See
// fuzz_main.cpp and the fuzz targets in the Makefile.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "captive.h"
#include "espn_parse.h"
#include "logo_lru.h"
#include "panel_text.h"
#include "poll_timing.h"
#include "recap.h"
#include "startup.h"

using namespace espn;

namespace {

// The firmware's ContainerReader: ArduinoJson reads the document a byte or a
// block at a time, the way it arrives from the socket. Small blocks put the
// block edges inside tokens.
class ChunkReader {
 public:
  ChunkReader(const uint8_t *data, size_t size, size_t chunk) : data_(data), size_(size), chunk_(chunk) {}
  int read() { return this->pos_ < this->size_ ? this->data_[this->pos_++] : -1; }
  size_t readBytes(char *dst, size_t n) {
    size_t take = std::min(std::min(n, this->chunk_), this->size_ - this->pos_);
    memcpy(dst, this->data_ + this->pos_, take);
    this->pos_ += take;
    return take;
  }

 private:
  const uint8_t *data_;
  size_t size_, chunk_, pos_{0};
};

// Which teams and events the input names, so the parsers that look for "our"
// team find one. Read with its own permissive filter.
struct Pick {
  std::string event_id;
  uint32_t team_id;
};

void add_pick(std::vector<Pick> &picks, const std::string &event_id, JsonVariantConst id) {
  if (picks.size() >= 6)
    return;
  uint32_t team = (uint32_t) atol(espn::detail::str_or_empty(id).c_str());
  if (team != 0)
    picks.push_back({event_id, team});
}

std::vector<Pick> find_picks(const uint8_t *data, size_t size) {
  JsonDocument filter;
  filter["id"] = true;
  filter["team"]["id"] = true;
  filter["competitions"][0]["competitors"][0]["id"] = true;
  filter["competitions"][0]["competitors"][0]["team"]["id"] = true;
  JsonObject ev = filter["events"].add<JsonObject>();
  ev["id"] = true;
  ev["competitions"][0]["competitors"][0]["id"] = true;
  ev["competitions"][0]["competitors"][0]["team"]["id"] = true;
  JsonDocument doc;
  std::vector<Pick> picks;
  if (deserializeJson(doc, (const char *) data, size, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(40)))
    return picks;
  add_pick(picks, "", doc["team"]["id"]);
  std::string root_id = espn::detail::str_or_empty(doc["id"]);
  JsonArrayConst root_comps = doc["competitions"][0]["competitors"];
  for (JsonObjectConst c : root_comps) {
    add_pick(picks, root_id, c["id"]);
    add_pick(picks, root_id, c["team"]["id"]);
  }
  for (JsonObjectConst e : doc["events"].as<JsonArrayConst>()) {
    std::string id = espn::detail::str_or_empty(e["id"]);
    for (JsonObjectConst c : e["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      add_pick(picks, id, c["id"]);
      add_pick(picks, id, c["team"]["id"]);
    }
  }
  return picks;
}

// ESPTime::from_epoch_local(epoch).to_c_tm() on the panel, in UTC. An epoch
// gmtime can't convert gives an invalid ESPTime, all zero, whose month and
// weekday come out as -1.
struct tm panel_tm(int64_t epoch) {
  struct tm out {};
  time_t t = (time_t) epoch;
  if (gmtime_r(&t, &out) == nullptr) {
    out = tm{};
    out.tm_mon = out.tm_wday = out.tm_yday = -1;
    out.tm_year = -1900;
  }
  return out;
}

// What the board and Home Assistant get from a snapshot, for every set of
// ticker switches that changes the text.
size_t render(const GameSnapshot &prev, const GameSnapshot &g, int64_t now) {
  size_t n = 0;
  int64_t at = g.time_tbd ? tbd_day_epoch(g.kickoff_epoch) : g.kickoff_epoch;
  std::string kick_local = kickoff_label(panel_tm(at), panel_tm(now), g.time_tbd);
  // The follow-up reads a parsed game leads to.
  for (uint8_t league = 0; league < kLeagueCount; league++) {
    n += scoreboard_url((League) league, 1, g.kickoff_epoch, g.time_tbd).size();
    n += team_day_url((League) league, 1, g.kickoff_epoch).size();
    n += event_url((League) league, g.event_id).size();
  }
  for (int bits = 0; bits < 4; bits++) {
    TickerOptions o;
    o.clock = o.down_distance = o.baseball_count = o.soccer_stats = (bits & 1) != 0;
    o.last_play = o.odds = (bits & 2) != 0;
    n += esphome::gameday::panel_text(status_text(g, o, kick_local)).size();
  }
  n += esphome::gameday::panel_text(clock_text(g)).size();
  n += ha_status_text(g, kick_local).size();
  for (int flags = 0; flags < 4; flags++)
    n += decide_splash(prev, g, (flags & 1) != 0, (flags & 2) != 0).text.size();
  n += parse_color(g.team_color) + parse_color(g.opp_color);
  // The logo cache's second try when a dark logo is missing.
  n += plain_logo_url(g.team_logo).size() + plain_logo_url(g.opp_logo).size();
  if (g.sport == Sport::BASEBALL)
    n += baseball_count(g).size();
  if (g.sport == Sport::SOCCER) {
    for (size_t max = 0; max <= 16; max += 4)
      n += soccer_situation(g, max).size();
    n += soccer_board_record(g.team_record).size() + soccer_board_record(g.opp_record).size();
  }
  if (g.sport == Sport::BASKETBALL)
    n += basketball_series(g).size();
  return n;
}

// The finals rotation as the live modes run it: absorb the scan, build a
// loop of cards and draw each one the way recap_show_() does.
size_t recap(std::vector<Final> finals, std::vector<Pending> pending, int64_t next_kickoff, int64_t now,
             uint8_t league) {
  for (auto &f : finals)
    f.league = league;
  for (auto &p : pending)
    p.league = league;
  std::vector<RecapDay> days;
  recap_absorb(days, finals, pending, now);
  // A second scan the next day with the same games, as the walk back reads.
  recap_absorb(days, finals, pending, now + 86400);
  size_t n = recap_count(days, league);
  int64_t next = recap_scan_next(next_kickoff, pending, now);
  std::vector<RecapCard> cards = recap_cards(days, 0xFFFF, next != 0, now);
  for (const RecapCard &c : cards) {
    n += recap_card_ms(c.kind);
    switch (c.kind) {
      case RecapKind::HEADER:
        n += recap_header(days.at(c.day), now).size();
        break;
      case RecapKind::FINAL:
        n += render(GameSnapshot{}, final_snapshot(days.at(c.day).finals.at(c.final)), now);
        n += recap_header(days.at(c.day), now).size();
        break;
      case RecapKind::LATER: {
        const Pending &p = days.at(c.day).later.at(c.final);
        n += render(GameSnapshot{}, pending_snapshot(p), now);
        std::string when = kickoff_label(panel_tm(p.kickoff_epoch), panel_tm(now));
        n += recap_later_ticker(p, when).size() + recap_matchup(p).size();
        break;
      }
      default:
        n += recap_next_text("", kickoff_label(panel_tm(next), panel_tm(now))).size();
        break;
    }
  }
  return n;
}

// One header value as the panel reads it: the Date header sets the clock
// when NTP is silent, Retry-After stretches the wait after a refusal, and a
// redirect's Location says whether a sign-in page answered for ESPN. Checks
// the ranges the callers rely on. rnd stands in for esp_random().
size_t header(const std::string &value, uint32_t rnd) {
  size_t n = 0;
  // 2020-01-01T00:00:00Z to the leap second at the end of 2099, or nothing.
  int64_t date = parse_http_date(value);
  if (date != 0 && (date < 1577836800 || date > 4102444800))
    abort();
  // 1970 to 9999-12-31T23:59:60Z; 0 is no date.
  int64_t iso = parse_iso8601_z(value);
  if (iso < 0 || iso > 253402300800)
    abort();
  // At most six digits of seconds.
  uint32_t retry_after = parse_retry_after_ms(value);
  if (retry_after > 999999000u)
    abort();
  // A run of failed reads, some refused: every wait stays inside its
  // schedule, Retry-After included.
  Backoff run;
  for (uint32_t i = 0; i < 20; i++) {
    uint32_t wait = run.fail(((rnd >> i) & 1) != 0, retry_after, rnd ^ (i * 2654435761u));
    uint32_t lo = run.throttled ? kThrottleBaseMs / 2 : kBackoffBaseMs / 2;
    uint32_t hi = run.throttled ? kThrottleCapMs + kThrottleCapMs / 4 : kBackoffCapMs;
    if (wait < lo || wait > hi)
      abort();
    n += wait;
  }
  std::string host = espn::detail::url_host(value);
  if (host.find_first_of(":/?#") != std::string::npos)
    abort();
  for (char c : host)
    if (c >= 'A' && c <= 'Z')
      abort();
  static const char kDotEspn[] = ".espn.com";
  const size_t dot = sizeof(kDotEspn) - 1;
  if (espn::detail::espn_host(host) && host != "espn.com" &&
      (host.size() < dot || host.compare(host.size() - dot, dot, kDotEspn) != 0))
    abort();
  return n + host.size();
}

// The sign-in probe's answer, with the input as its Location, Content-Type
// and body, for each status the probe tells apart.
size_t probe(const std::string &text) {
  static const int kStatuses[] = {-1, 0, 200, 204, 301, 302, 307, 403, 404, 503, 511};
  size_t n = 0;
  for (int status : kStatuses) {
    Probe p = classify_probe(status, text, text, text);
    if (status <= 0 && p != Probe::UNKNOWN)
      abort();  // no answer proves nothing
    if (status == 511 && p != Probe::CAPTIVE)
      abort();  // the status made for a sign-in page
    if (status >= 300 && status < 400 && espn::detail::url_host(text).empty() && p != Probe::UNKNOWN)
      abort();  // a relative redirect stays on whoever answered
    n += strlen(probe_name(p));
  }
  return n;
}

// The boot cache (startup.h) a team read leaves in flash: it loads back as
// the same game or not at all, and only for the team it was saved for.
size_t boot_cache(const Schedule &team, int64_t now) {
  TeamCache c;
  if (!team_cache_save(team, League::NFL, 6, now, c))
    return 0;
  Schedule back;
  const bool loaded = team_cache_load(c, League::NFL, 6, now, back);
  const std::string id = team.event_id.c_str();
  const bool want = team.comp_slug == nullptr && !team.time_tbd && !id.empty() &&
                    team.kickoff_epoch >= now - TEAM_CACHE_GAME_OVER;
  if (loaded != want)
    abort();
  if (loaded && (back.event_id != id || back.kickoff_epoch != team.kickoff_epoch || back.group != team.group ||
                 back.league != (uint8_t) League::NFL))
    abort();
  if (team_cache_load(c, League::NFL, 7, now, back) || team_cache_load(c, League::NCAA, 6, now, back))
    abort();
  return back.event_id.size();
}

const Sport kSports[] = {Sport::FOOTBALL, Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // The clock of the day the parsers are run on: 2026-10-17, noon Eastern.
  const int64_t now = 1792252800;
  // Keeps the work from being optimized away.
  volatile size_t sink = 0;
  size_t chunk = size > 0 ? 1 + data[0] % 64 : 1;
  std::string json((const char *) data, size);
  std::vector<Pick> picks = find_picks(data, size);

  // The headers, a line each like an HTTP response's, and the probe.
  uint32_t rnd = 2166136261u;
  for (size_t i = 0; i < size && i < 64; i++)
    rnd = (rnd ^ data[i]) * 16777619u;
  sink = sink + header(json, rnd) + probe(json);
  const bool multiline = json.find_first_of("\r\n") != std::string::npos;
  for (size_t start = 0, lines = 0; multiline && start < size && lines < 8; lines++) {
    size_t end = json.find_first_of("\r\n", start);
    if (end == std::string::npos)
      end = size;
    sink = sink + header(json.substr(start, end - start), rnd);
    start = end + 1;
  }

  for (uint8_t league = 0; league < kLeagueCount; league += 4) {
    std::vector<LiveGame> live;
    std::vector<Final> finals;
    std::vector<Pending> pending;
    int64_t next = 0;
    ChunkReader r(data, size, chunk);
    if (parse_live_games(r, live, &next, &finals, &pending))
      sink = sink + recap(finals, pending, next, now, league);
  }
  {
    // The plain scan, with nothing kept for the rotation.
    std::vector<LiveGame> live;
    ChunkReader r(data, size, chunk);
    parse_live_games(r, live);
    for (const auto &g : live)
      sink = sink + g.away_abbr.size() + g.home_abbr.size();
  }

  Schedule team;
  {
    ChunkReader r(data, size, chunk);
    if (parse_team(r, team)) {
      std::vector<Schedule> comps{team, team};
      comps[1].kickoff_epoch = now + 3600;
      sink = sink + (size_t) (pick_schedule(comps, now, team.event_id) + 1);
      sink = sink + boot_cache(team, now);
    }
  }

  GameSnapshot prev;
  for (const Pick &p : picks) {
    std::vector<Upcoming> up;
    ChunkReader r1(data, size, chunk);
    if (parse_upcoming(r1, p.team_id, 5, up))
      for (const auto &u : up)
        sink = sink + u.opp_name.size() + u.tv.size();

    Schedule day;
    ChunkReader r2(data, size, chunk);
    if (parse_team_game(r2, p.team_id, "", day) == 1)
      sink = sink + (today_game_first(team, day) ? 1 : 0);
    sink = sink + parse_team_game_str(json, p.team_id, day, p.event_id);

    GameSnapshot g;
    ChunkReader r3(data, size, chunk);
    if (parse_scoreboard(r3, p.event_id, p.team_id, g)) {
      sink = sink + render(prev, g, now);
      prev = g;
    }

    for (Sport sport : kSports) {
      GameSnapshot e;
      ChunkReader r4(data, size, chunk);
      if (!parse_event(r4, sport, p.team_id, e, p.event_id))
        continue;
      sink = sink + render(prev, e, now);
      // The same game a poll later: what a splash compares.
      sink = sink + render(e, e, now);
      prev = e;
    }
  }
  (void) sink;
  return 0;
}
