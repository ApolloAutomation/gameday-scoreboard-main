// Fuzz target: one input goes through every ESPN parser the panel runs, the
// text built from what they read, and the finals rotation (recap.h). ESPN's
// documents share one shape (events, competitions, competitors), so a
// mutated scoreboard also reaches the event and team parsers. The same input,
// line by line, is also read as the response headers the panel acts on (Date,
// Retry-After) and as the answer to the sign-in probe (captive.h). See
// fuzz_main.cpp and the fuzz targets in the Makefile.
// Also runs MLB Stats, HockeyTech, F1, market and flight parsers.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "app_frame_check.h"
#include "captive.h"
#include "espn_parse.h"
#include "app_f1.h"
#include "f1.h"
#include "flights.h"
#include "plane_photo.h"
#include "logo_icon.h"
#include "logo_lru.h"
#include "markets_view.h"
#include "panel_text.h"
#include "players.h"
#include "props.h"
#include "poll_timing.h"
#include "recap.h"
#include "source_hockeytech.h"
#include "source_markets.h"
#include "source_mlbstats.h"
#include "startup.h"
#include "watch.h"

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
  // MLB Stats: dates[].games[], team ids are numbers
  JsonObject mg = filter["dates"].add<JsonObject>()["games"].add<JsonObject>();
  mg["gamePk"] = true;
  mg["teams"]["away"]["team"]["id"] = true;
  mg["teams"]["home"]["team"]["id"] = true;
  // HockeyTech: scorebar, day and clock; ids are strings
  JsonObject hb = filter["SiteKit"]["Scorebar"].add<JsonObject>();
  for (const char *k : {"ID", "HomeID", "VisitorID"})
    hb[k] = true;
  JsonObject hd = filter["SiteKit"]["Gamesbydate"].add<JsonObject>();
  for (const char *k : {"id", "home_team", "visiting_team"})
    hd[k] = true;
  filter["GC"]["Clock"]["home_team"]["team_id"] = true;
  filter["GC"]["Clock"]["visiting_team"]["team_id"] = true;
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
  for (JsonObjectConst d : doc["dates"].as<JsonArrayConst>())
    for (JsonObjectConst g : d["games"].as<JsonArrayConst>())
      for (const char *side : {"away", "home"}) {
        uint32_t team = g["teams"][side]["team"]["id"] | 0u;
        if (team != 0 && picks.size() < 6)
          picks.push_back({std::to_string(g["gamePk"] | 0u), team});
      }
  for (JsonObjectConst g : doc["SiteKit"]["Scorebar"].as<JsonArrayConst>()) {
    add_pick(picks, espn::detail::str_or_empty(g["ID"]), g["HomeID"]);
    add_pick(picks, espn::detail::str_or_empty(g["ID"]), g["VisitorID"]);
  }
  for (JsonObjectConst g : doc["SiteKit"]["Gamesbydate"].as<JsonArrayConst>()) {
    add_pick(picks, espn::detail::str_or_empty(g["id"]), g["home_team"]);
    add_pick(picks, espn::detail::str_or_empty(g["id"]), g["visiting_team"]);
  }
  add_pick(picks, "", doc["GC"]["Clock"]["home_team"]["team_id"]);
  add_pick(picks, "", doc["GC"]["Clock"]["visiting_team"]["team_id"]);
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

size_t players(const GameSnapshot &prev, const GameSnapshot &g, uint8_t league) {
  static const uint32_t kIds[] = {4870883, 5079684, 4917849, 4396909, 4431671, 280555, 3918298, 199680,
                                  4431680, 5080761, 3114727, 4882093, 4433137, 3916433, 3899937, 218522};
  static_assert(sizeof(kIds) / sizeof(kIds[0]) == kPlayerSlots, "an id per slot");
  FavPlayer list[kPlayerSlots]{};
  for (int i = 0; i < kPlayerSlots; i++)
    list[i] = make_player(league, kIds[i], i % 2 ? g.team_id : g.opp_id, i % 2 ? "B. TKACHUK" : "CONNOR");
  size_t n = 0;
  for (const auto &line : player_lines(g, league, list))
    n += esphome::gameday::panel_text(line).size();
  for (bool wide : {false, true})
    n += board_splash_with_players(prev, g, true, false, kAlertsAll, wide, league, list).text.size();
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
  if (loaded && (back.seed != team.seed || back.place != team.place.substr(0, sizeof(c.place) - 1)))
    abort();
  if (team_cache_load(c, League::NFL, 7, now, back) || team_cache_load(c, League::NCAA, 6, now, back))
    abort();
  return back.event_id.size();
}

// No word of a wrapping label longer than its line: LVGL would break it in
// the middle.
static void words_fit(const std::string &text, size_t line) {
  size_t word = 0;
  for (char c : text) {
    word = c == ' ' || c == '\n' || c == '-' ? 0 : word + 1;
    if (word > line)
      abort();
  }
}

// A golf tour's board from its header: the columns, the card, the ticker and
// Home Assistant, the splash and the timing, with next as the calendar's next
// event. Every column stays inside its width at both board widths.
size_t golf(GameSnapshot g, const GolfNext &next, int64_t now) {
  size_t n = 0;
  for (auto &r : g.golf.rows)
    r.set_tee(r.tee_epoch > 0 ? golf_tee_label(panel_tm(r.tee_epoch)) : "");
  if (next.valid)
    g.golf.next = next;
  std::string dates = golf_dates(panel_tm(tbd_day_epoch(g.kickoff_epoch)), panel_tm(tbd_day_epoch(g.golf.end_epoch)));
  for (bool wide : {false, true}) {
    BoardLines l = golf_lines(g, wide);
    if (l.status.size() > 12 || (wide ? l.title.size() + 1 + l.status.size() > 25 : !l.title.empty()))
      abort();
    if (l.pos.size() > kGolfBoardRows || l.name.size() != l.pos.size() || l.value.size() != l.pos.size() ||
        l.detail.size() != l.pos.size() || l.value_color.size() != l.pos.size() || l.name_color.size() != l.pos.size())
      abort();
    for (size_t i = 0; i < l.pos.size(); i++)
      if (l.pos[i].size() > 2 || l.name[i].size() > (wide ? 11u : 7u) || l.value[i].size() > 3 ||
          l.detail[i].size() > (wide ? 6u : 0u))
        abort();
    std::string card = golf_card_text(g, wide, dates);
    if (card.substr(0, card.find('\n')).size() > (wide ? 42u : 36u))
      abort();
    words_fit(card, wide ? 21 : 12);
    n += card.size() + l.title.size();
  }
  n += render(GameSnapshot{}, g, now);
  GameSnapshot before = g;
  before.golf.phase = GolfPhase::LIVE;
  for (bool wide : {false, true}) {
    std::string splash = golf_splash(before, g, wide).text;
    words_fit(splash, wide ? 16 : 10);
    n += splash.size();
  }
  n += golf_poll_ms(g, now) + (golf_off_season(g, now) ? 1 : 0) + (golf_show_next(g, now) ? 1 : 0) +
       (golf_read_is_older(before, g) ? 1 : 0) + (golf_read_is_older(g, before) ? 1 : 0);
  n += golf_status_text(golf_next_snapshot(g.golf.next), TickerOptions{}, dates).size();
  return n;
}

// A racing series' board from its header: the columns at both widths (no
// car numbers or gaps on one panel), the card, the ticker and Home
// Assistant, the splash and the timing, with next as the calendar's next
// race weekend.
size_t racing(GameSnapshot g, const GolfNext &next, int64_t now) {
  size_t n = 0;
  if (next.valid)
    g.race.next = next;
  std::string when = g.race.next_start > 0 ? racing_when(panel_tm(g.race.next_start), panel_tm(now))
                                           : golf_dates(panel_tm(g.kickoff_epoch), panel_tm(g.race.race_epoch));
  for (bool wide : {false, true}) {
    BoardLines l = racing_lines(g, wide);
    if (l.status.size() > 12 || (wide ? l.title.size() + 1 + l.status.size() > 25 : !l.title.empty()))
      abort();
    if (l.pos.size() > kGolfBoardRows || l.name.size() != l.pos.size() || l.value.size() != l.pos.size() ||
        l.detail.size() != l.pos.size() || l.value_color.size() != l.pos.size() || l.name_color.size() != l.pos.size())
      abort();
    for (size_t i = 0; i < l.pos.size(); i++)
      if (l.pos[i].size() > 2 || l.name[i].size() > (wide ? 11u : 10u) || l.value[i].size() > (wide ? 3u : 0u) ||
          l.detail[i].size() > (wide ? 6u : 0u))
        abort();
    std::string card = racing_card_text(g, wide, when);
    if (card.substr(0, card.find('\n')).size() > (wide ? 21u : 24u))
      abort();
    words_fit(card, wide ? 21 : 12);
    n += card.size() + l.title.size();
  }
  n += render(GameSnapshot{}, g, now);
  n += racing_status_text(g, TickerOptions{}, when).size() + racing_ha_status_text(g, when).size();
  n += racing_state_long(g).size();
  for (const auto &r : g.race.rows)
    n += racing_detail_long(r).size() + racing_gap(r.gap, g.race.rows[0].gap).size();
  GameSnapshot before = g;
  before.race.phase = RacePhase::LIVE;
  for (bool wide : {false, true}) {
    std::string splash = racing_splash(before, g, wide).text;
    words_fit(splash, wide ? 16 : 10);
    n += splash.size();
  }
  n += racing_poll_ms(g, now) + (racing_off_season(g, now) ? 1 : 0) + (racing_show_next(g, now) ? 1 : 0) +
       (racing_on_track(g, now) ? 1 : 0) +
       (racing_read_is_older(before, g) ? 1 : 0) + (racing_read_is_older(g, before) ? 1 : 0);
  n += racing_status_text(racing_next_snapshot(g.race.next), TickerOptions{}, when).size();
  return n;
}

// Every F1 frame part inside its column, at each width.
static size_t f1_fits(const F1Frame &f) {
  const F1Geom g = f1_geom(f.layout, f.width);
  auto glyphs = [](const char *s) {
    size_t n = 0;
    for (; *s != '\0'; s++)
      n += ((unsigned char) *s & 0xC0) != 0x80;
    return n;
  };
  if (f.n > g.rows || glyphs(f.status.c_str()) > g.status.chars ||
      glyphs(f.title.c_str()) + glyphs(f.status.c_str()) > g.status.chars || f.shape_n > kF1ShapeMax)
    abort();
  for (size_t i = 0; i < f.n; i++) {
    const F1Row &r = f.rows[i];
    if (glyphs(r.pos) > g.pos.chars || glyphs(r.name) > g.name.chars || glyphs(r.value) > g.value.chars ||
        glyphs(r.detail) > g.detail.chars)
      abort();
  }
  for (size_t i = 0; i < f.shape_n; i++)
    if (f.shape[i].x < g.shape_x || f.shape[i].x >= g.shape_x + g.shape_w || f.shape[i].y < g.shape_y ||
        f.shape[i].y >= g.shape_y + g.shape_h)
      abort();
  return f.ticker.size() + f.spans.size();
}

// The F1 display's reads and every frame they make.
size_t f1(const uint8_t *data, size_t size, size_t chunk, int64_t now) {
  size_t n = 0;
  const F1Units units[] = {F1Units{}, F1Units{true, true}};
  F1Live live;
  ChunkReader r1(data, size, chunk);
  const bool have_live = parse_f1_live(r1, live) == 1;
  F1Schedule sched;
  ChunkReader r2(data, size, chunk);
  const bool have_sched = parse_f1_race(r2, sched) == 1;
  F1Standings drivers, teams;
  ChunkReader r3(data, size, chunk);
  parse_f1_standings(r3, true, drivers);
  ChunkReader r4(data, size, chunk);
  parse_f1_standings(r4, false, teams);
  F1Results res;
  ChunkReader r5(data, size, chunk);
  parse_f1_results(r5, res);
  F1CircuitFacts facts;
  ChunkReader r6(data, size, chunk);
  parse_f1_circuit(r6, facts);
  F1Athlete ath;
  ChunkReader r7(data, size, chunk);
  if (parse_f1_athlete(r7, ath) == 1) {
    memcpy(facts.record_code, ath.code, sizeof(ath.code));
    memcpy(facts.record_name, ath.name, sizeof(ath.name));
  }
  F1Weather wx;
  ChunkReader r8(data, size, chunk);
  parse_f1_weather(r8, wx);
  F1Weather met;
  ChunkReader r9(data, size, chunk);
  if (parse_met_weather(r9, now, met) == 1 && !met.valid)
    abort();
  if (sched.n > kF1Sessions || drivers.n > kF1Keep || teams.n > kF1Keep || res.n > kF1Keep)
    abort();
  if (have_live)
    f1_live_codes(live, drivers);
  const CircuitInfo *c = have_sched ? circuit_for(sched) : circuit_by_espn(facts.espn_id);
  if (c == nullptr && have_live)
    c = circuit_by_name(live.snap.venue);
  for (int w : {64, 128, 192, 256}) {
    if (have_live)
      n += f1_fits(f1_live_frame(live, w, "Today 2:00p"));
    n += f1_fits(f1_next_frame(sched, now, panel_tm, w));
    for (const F1Units &u : units) {
      n += f1_fits(f1_circuit_frame(c, facts, u, w));
      n += f1_fits(f1_weather_frame(wx, sched.locality, u, w));
    }
    n += f1_fits(f1_standings_frame(drivers, w)) + f1_fits(f1_standings_frame(teams, w));
    n += f1_fits(f1_results_frame(res, w));
  }
  n += esphome::gameday::ticker_markup(esphome::gameday::panel_text(f1_standings_frame(drivers, 128).ticker),
                                       f1_standings_frame(drivers, 128).spans)
           .size();
  for (F1Read r : {F1Read::NEXT, F1Read::DRIVERS, F1Read::LAST_RESULTS, F1Read::WEATHER, F1Read::CIRCUIT})
    n += f1_poll_ms(r, sched, now);
  n += f1_weather_url(sched.lat_e4, sched.lon_e4).size() + f1_circuit_url(facts.record_athlete).size();
  return n;
}

// The F1 display fed the input as every read.
size_t f1_app(const uint8_t *data, size_t size, int64_t now) {
  F1App a;
  for (int i = 0; i < 12; i++) {
    AppRequest rq;
    if (!a.request(now, rq))
      break;
    JsonDocument filter, doc;
    const bool ok = a.filter(filter) &&
                    !deserializeJson(doc, (const char *) data, size, DeserializationOption::Filter(filter),
                                     DeserializationOption::NestingLimit(40)) &&
                    a.parse_doc(doc.as<JsonObjectConst>(), now);
    a.ended(AppResult{ok ? 200 : 500, false, 0});
    a.apply(ok, now);
  }
  auto glyphs = [](const std::string &s) {
    size_t n = 0;
    for (char c : s)
      n += ((unsigned char) c & 0xC0) != 0x80;
    return n;
  };
  size_t n = 0;
  for (int w : {64, 128, 192, 256})
    for (int64_t t = now; t < now + 60; t += 7) {
      AppFrame f;
      a.frame(w, 64, t, f);
      const BoardLines &b = f.rows;
      const bool wide = w >= 128;
      if (b.name.size() > 6 || b.pos.size() != b.name.size() || b.value.size() != b.name.size() ||
          b.detail.size() != b.name.size() || glyphs(b.status) > (wide ? (size_t) (w - 2) / 5 : 12) ||
          (wide && !b.title.empty() &&
           glyphs(b.title) + glyphs(b.status) + (b.status.empty() ? 0 : 1) > (size_t) (w - 2) / 5))
        abort();
      for (size_t i = 0; i < b.name.size(); i++)
        if (glyphs(b.pos[i]) > 2 || glyphs(b.name[i]) > (wide ? 11 : b.value_narrow ? 7 : 10) ||
            glyphs(b.value[i]) > 3 || glyphs(b.detail[i]) > 6)
          abort();
      for (const AppPoint &p : f.line.pts)
        if (p.x < 0 || p.x >= w || p.y < 8 || p.y >= 56)
          abort();
      n += f.ticker.size() + esphome::gameday::ticker_markup(esphome::gameday::panel_text(f.ticker), f.spans).size();
    }
  // Full screen on a 2x2: every card inside 128x128
  for (int64_t t = now; t < now + 60; t += 7) {
    AppFrame f;
    a.frame(128, 128, t, f);
    if (!app_frame_problems(f, 128, 128).empty())
      abort();
    n += f.texts.size();
  }
  return n + a.poll_ms(now) + (a.busy(now) ? 1 : 0);
}

const Sport kSports[] = {Sport::FOOTBALL, Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER};

// Every sparkline point inside its box.
void spark_inside(const SparkLine &l, int x, int y, int w, int h) {
  for (int i = 0; i < l.n; i++)
    if (l.pts[i].x < x || l.pts[i].x >= x + w || l.pts[i].y < y || l.pts[i].y >= y + h)
      abort();
  if (l.base_y >= 0 && (l.base_y < y || l.base_y >= y + h))
    abort();
}

// Stocks, an index, ETFs and coins, one of each source.
MarketBlob market_list() {
  MarketBlob b{};
  for (const char *t : {"AAPL", "^GSPC", "BRK.B", "NDX", "BTC", "DOGE", "SHIB", "HBAR-USD"}) {
    MarketSymbol s{};
    if (parse_market_symbol(t, MarketKind::NONE, s))
      market_put(b, 0, s);
  }
  return b;
}

// The three quote reads, then every frame drawn from them.
size_t markets(const uint8_t *data, size_t size, size_t chunk, int64_t now) {
  static const MarketBlob list = market_list();
  size_t n = 0;
  MarketQuote q[kMarketSlots];
  ChunkReader r1(data, size, chunk);
  int found = parse_yahoo_quotes(r1, list, q);
  ChunkReader r2(data, size, chunk);
  found += parse_kraken_quotes(r2, list, now, q);
  ChunkReader r3(data, size, chunk);
  found += parse_nasdaq_quotes(r3, list, now, q);
  if (found > 3 * kMarketSlots)
    abort();
  for (int w : {64, 128, 256, 128 + 1}) {
    // 129 stands for full screen's 128x128
    const MarketGeom g = w == 129 ? market_geom(128, 128, true, true, true, true) : market_geom(w, 64, true, true);
    const MarketFrame f = market_frame(list, q, g, now, 1, true);
    if (f.n > g.rows || f.page >= f.pages)
      abort();
    spark_inside(f.line, g.spark_x, g.spark_y, g.spark_w, g.spark_h);
    for (int i = 0; i < f.n; i++)
      n += f.rows[i].price.size() + f.rows[i].change.size() + f.rows[i].pct.size();
  }
  for (int i = 0; i < kMarketSlots; i++) {
    const MarketCard c = market_card(list.syms[i], q[i], 64, 64, true, now);
    spark_inside(c.line, 0, 0, 64, 56);
    n += c.price.size() + c.status.size() + market_state_text(market_state(q[i], MarketKind::STOCK, now), q[i], now,
                                                               true).size();
  }
  std::vector<esphome::gameday::TickerSpan> spans;
  const std::string line = market_ticker(list, q, now, &spans);
  return n + esphome::gameday::ticker_markup(line, spans).size() + market_poll_ms(list, q, now);
}

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
  // A summary's box score, cut where the panel stops reading.
  for (Sport sport : {Sport::FOOTBALL, Sport::BASKETBALL, Sport::BASEBALL, Sport::HOCKEY}) {
    PlayerWatch w;
    for (uint32_t id : {3918298u, 4431680u, 5080761u, 3114727u, 4882093u})
      w.ids[w.n++] = id;
    for (uint32_t id = 9000; w.n < PlayerWatch::kMax; id++)
      w.ids[w.n++] = id;
    BoxCutter cut;
    StringReader in(json, &cut);
    std::vector<BoxLine> lines;
    if (!parse_box(in, sport, w, lines))
      continue;
    if (lines.size() > (size_t) PlayerWatch::kMax || cut.used() > size)
      abort();
    for (size_t stat = 0; stat < kPropStatCount; stat++) {
      if (kPropStats[stat].sport != sport)
        continue;
      Prop pr{};
      pr.athlete = lines.empty() ? 1 : lines[0].athlete;
      pr.stat = (uint8_t) stat;
      pr.side = (uint8_t) (kPropStats[stat].yes ? PropSide::YES : PropSide::OVER);
      pr.line = kPropStats[stat].yes ? 0 : 49;
      strncpy(pr.name, "ALLEN", kPropNameMax);
      for (const BoxLine &l : lines) {
        const int v = prop_value(pr, l);
        BetGame g;
        g.event = 1;
        g.phase = BetPhase::LIVE;
        pr.event = 1;
        sink = sink + prop_ticker(pr, prop_status(pr, &g, v, 500, now), v).size();
        prop_grade(pr, l.played, v, now, now + 3600);
        sink = sink + prop_ticker(pr, PropStatus::WON, v).size();
      }
    }
  }
  {
    // The plain scan, with nothing kept for the rotation.
    std::vector<LiveGame> live;
    ChunkReader r(data, size, chunk);
    parse_live_games(r, live);
    for (const auto &g : live)
      sink = sink + g.away_abbr.size() + g.home_abbr.size();
  }

  for (League league : {League::NFL, League::MLB, League::NBA, League::NHL, League::MLS}) {
    // The big-play watcher's day read, twice.
    WatchJob first;
    first.league = (uint8_t) league;
    first.ctx.live = true;
    first.ctx.mask = kAlertsAll;
    ChunkReader r(data, size, chunk);
    if (!watch_read_day(r, first))
      continue;
    WatchJob second = first;
    second.games = first.fresh;
    second.fresh.clear();
    ChunkReader r2(data, size, chunk);
    watch_read_day(r2, second);
    std::vector<WatchGame> games = first.fresh;
    WatchScope scope;
    scope.kind = WatchScope::SOURCE;
    scope.league = (uint8_t) league;
    watch_merge(games, second.fresh, scope);
    if (games.size() > kWatchMax)
      abort();
    sink = sink + games.size() + second.alert.splash.text.size();
  }

  {
    // Flight Tracker: planes, their routes, frames and the ticker.
    flights::Home home;
    home.lat = 40.64;
    home.lon = -73.78;
    home.radius_nm = 250;
    flights::Nearby near;
    flights::RouteCache cache;
    ChunkReader r(data, size, chunk);
    if (flights::parse_aircraft(r, home, near)) {
      if (near.n > flights::kNear)
        abort();
      const flights::RouteAsk ask = flights::route_asks(near, cache, 100);
      sink = sink + flights::routeset_body(ask).size();
      for (int w : {64, 128, 192, 256}) {
        flights::FrameOpts o;
        o.w = w;
        o.card = flights::pick_card(near, near.planes[near.n > 1 ? 1 : 0].hex);
        const flights::Frame f = flights::flight_frame(near, &cache, o, 100);
        for (const flights::Text &t : f.items)
          if (flights::text_px(t) > t.w || t.x < 0 || t.x + t.w > w)
            abort();
        o.list = true;
        sink = sink + flights::flight_frame(near, &cache, o, 100).items.size();
        sink = sink + f.ticker.size() + flights::note_frame(flights::Note::NONE_NEAR, o, 10).items.size();
      }
    }
    ChunkReader r2(data, size, chunk);
    if (flights::parse_fr24(r2, home, 1794457500, near) && near.n > flights::kNear)
      abort();
    // Pictures and logos stay inside the card.
    for (int w : {64, 128, 256}) {
      flights::FrameOpts o;
      o.w = w;
      const flights::Frame f = flights::flight_frame(near, &cache, o, 100);
      for (const flights::Art &a : f.art) {
        const flights::PlaneSprite *s = flights::plane_sprite(a.kind);
        if (s == nullptr || a.x < 0 || a.y < 0 || a.x + s->w > w || a.y + s->h > o.h - 8)
          abort();
      }
      for (const flights::Logo &l : f.logos)
        if (l.x < 0 || l.x + flights::kLogoPx > w || flights::airline_by_icao(l.icao) == nullptr ||
            l.alt >= (int) f.items.size())
          abort();
      sink = sink + f.art.size() + f.logos.size();
    }
    {
      // The logo steps on the bytes as an image.
      const int side = 1 + (int) (size % 32);
      std::vector<uint8_t> img(espn::logo_icon_bytes(side, side), 0);
      memcpy(img.data(), data, std::min(size, img.size()));
      std::vector<uint16_t> stack;
      espn::logo_icon_knockout(img.data(), side, side, stack);
      std::vector<uint8_t> icon = espn::logo_icon_shrink(img.data(), side, side, 16);
      espn::logo_icon_lift(icon.data(), 16, 16, true);
      sink = sink + espn::logo_icon_usable(icon.data(), 16) + espn::logo_placeholder(espn::kGoogleLogoUrl, data, size);
    }
    {
      // A photo answer, its card, the photo steps.
      flights::PlanePhoto ph;
      if (flights::parse_photo(json, ph) && ph.state == flights::PhotoState::KNOWN &&
          (strncmp(ph.url, "https://", 8) != 0 || strlen(ph.url) >= sizeof ph.url || strlen(ph.by) >= sizeof ph.by))
        abort();
      flights::FrameOpts o;
      o.w = 128 + (int) (size % 3) * 64;
      o.h = o.w == 128 && size % 2 == 1 ? 128 : 64;
      o.photo = &ph;
      const flights::Frame f = flights::flight_frame(near, &cache, o, 100);
      if (!f.photo.url.empty() && (f.photo.x + f.photo.w > o.w || f.photo.y + f.photo.h > o.h - 8 ||
                                   f.photo.at > f.ticker.size()))
        abort();
      const int bw = 1 + (int) (size % 128), bh = 1 + (int) (size / 3 % 80);
      const flights::PhotoFit fit = flights::photo_fit(1 + (uint32_t) (size % 1100), 1 + (uint32_t) (size / 7 % 900),
                                                       size & 1, bw, bh);
      if (fit.ok()) {
        // Bigger than its cut only when grown to fill
        const bool grown = 2 * fit.cw <= bw && 2 * fit.ch <= bh;
        if ((!grown && (fit.ow > fit.cw || fit.oh > fit.ch)) || fit.ow > bw || fit.oh > bh ||
            fit.cy + fit.ch > fit.sh)
          abort();
        std::vector<uint16_t> px((size_t) fit.sw * fit.sh, 0);
        memcpy(px.data(), data, std::min(size, px.size() * 2));
        std::vector<uint8_t> rgb((size_t) fit.ow * fit.oh * 3), out((size_t) fit.ow * fit.oh * 2);
        flights::photo_scale(px.data(), fit, rgb.data());
        flights::photo_light(rgb.data(), fit.ow, fit.oh, out.data());
        sink = sink + out[0];
      }
    }
    flights::RouteAsk ask;
    for (const char *cs : {"UAL755", "JBU1407", "DAL2064", "FIN16"})
      strcpy(ask.callsign[ask.n++], cs);
    flights::parse_routeset(json, ask, cache, 200);
    flights::parse_adsbdb(json, "AAL1070", cache, 200);
    for (const flights::Route &rt : cache.slots) {
      if (rt.n > flights::kStops)
        abort();
      for (size_t i = 0; i < near.n; i++)
        sink = sink + flights::leg_text(flights::route_leg(rt, near.planes[i]), " > ").size();
      flights::Plane p;
      p.lat = 40.4f;
      p.lon = -74.2f;
      p.track = 52;
      sink = sink + flights::leg_text(flights::route_leg(rt, p), "-").size();
    }
  }

  {
    // A golf tour: its header, then the calendar for what comes after it
    GameSnapshot g;
    GolfNext next;
    ChunkReader r(data, size, chunk);
    int found = parse_golf(r, g);
    ChunkReader r2(data, size, chunk);
    int ahead = parse_golf_calendar(r2, now, g.event_id, next);
    if ((ahead == 1) != next.valid)
      abort();
    if (found == 1)
      sink = sink + golf(g, next, now);
    else if (ahead == 1)
      sink = sink + golf(golf_next_snapshot(next), GolfNext{}, now);
  }

  sink = sink + f1(data, size, chunk, now);
  sink = sink + f1_app(data, size, now);

  {
    // A racing series: its header, then the calendar for the next race weekend
    GameSnapshot g;
    GolfNext next;
    ChunkReader r(data, size, chunk);
    int found = parse_racing(r, g);
    ChunkReader r2(data, size, chunk);
    int ahead = parse_racing_calendar(r2, now, g.event_id, g.kickoff_epoch, next);
    if ((ahead == 1) != next.valid || (found == 1) != g.valid || g.race.rows.size() > kRaceRows)
      abort();
    if (found == 1)
      sink = sink + racing(g, next, now);
    else if (ahead == 1)
      sink = sink + racing(racing_next_snapshot(next), GolfNext{}, now);
  }

  {
    // Minor league baseball's day scan
    std::vector<LiveGame> live;
    std::vector<Final> finals;
    std::vector<Pending> pending;
    int64_t next = 0;
    ChunkReader r(data, size, chunk);
    if (parse_mlb_day(r, live, &next, &finals, &pending)) {
      if (finals.size() > kRecapCap || pending.size() > kPendingKeep)
        abort();
      sink = sink + recap(finals, pending, next, now, (uint8_t) League::MILB);
    }
  }

  {
    // The AHL's or ECHL's day scan
    std::vector<LiveGame> live;
    std::vector<Final> finals;
    std::vector<Pending> pending;
    int64_t next = 0;
    ChunkReader r(data, size, chunk);
    if (parse_ht_day(r, live, &next, &finals, &pending)) {
      if (finals.size() > kRecapCap || pending.size() > kPendingKeep)
        abort();
      sink = sink + recap(finals, pending, next, now, (uint8_t) League::ECHL);
    }
  }

  Schedule team;
  {
    ChunkReader r(data, size, chunk);
    if (parse_team(r, team)) {
      std::vector<Schedule> comps{team, team};
      comps[1].kickoff_epoch = now + 3600;
      comps[1].place += " cup";  // a cup's own table, which the league's replaces
      league_standing(comps);
      if (comps[1].place != team.place || comps[1].seed != team.seed)
        abort();
      sink = sink + (size_t) (pick_schedule(comps, now, team.event_id) + 1);
      // The cut place must fit beside any clock.
      const size_t room = record_row_chars((int) (size % 80));
      if (board_standing(team.place, room).size() > room)
        abort();
      sink = sink + boot_cache(team, now);
    }
  }

  GameSnapshot prev;
  for (const Pick &p : picks) {
    std::vector<Upcoming> up;
    ChunkReader r1(data, size, chunk);
    if (parse_upcoming(r1, p.team_id, 5, up, true))
      for (const auto &u : up)
        sink = sink + u.opp_name.size() + u.tv.size() + u.opp_rank;

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

    // Minor league baseball team and game reads
    Schedule ms;
    std::vector<Upcoming> mup;
    ChunkReader r5(data, size, chunk);
    int found = parse_mlb_team(r5, p.team_id, now, ms, &mup, 4);
    if (found > 0 && (ms.event_id.empty() || mup.size() > 4))
      abort();
    if (found > 0)
      sink = sink + boot_cache(ms, now) + mup.size();
    GameSnapshot mg;
    ChunkReader r6(data, size, chunk);
    if (parse_mlb_game(r6, p.team_id, now, mg)) {
      if (mg.sport != Sport::BASEBALL || mg.mlb.balls > 4 || mg.mlb.outs > 3)
        abort();
      sink = sink + render(prev, mg, now);
      sink = sink + render(mg, mg, now);
      prev = mg;
    }

    // AHL and ECHL team and game centre reads
    Schedule hs;
    std::vector<Upcoming> hup;
    ChunkReader r7(data, size, chunk);
    int listed = parse_ht_team(r7, p.team_id, now, hs, &hup, 4);
    if (listed > 0 && (hs.event_id.empty() || hup.size() > 4))
      abort();
    if (listed > 0)
      sink = sink + boot_cache(hs, now) + hup.size();
    GameSnapshot hg;
    ChunkReader r8(data, size, chunk);
    if (parse_ht_game(r8, League::AHL, p.team_id, hg)) {
      if (hg.sport != Sport::HOCKEY || hg.period < 0 || hg.period > 20)
        abort();
      ht_fill_from_schedule(hg, hs);
      sink = sink + render(prev, hg, now);
      sink = sink + render(hg, hg, now);
      prev = hg;
    }

    for (Sport sport : kSports) {
      GameSnapshot e;
      ChunkReader r4(data, size, chunk);
      if (!parse_event(r4, sport, p.team_id, e, p.event_id))
        continue;
      sink = sink + render(prev, e, now);
      // The same game a poll later: what a splash compares.
      sink = sink + render(e, e, now);
      for (const auto &l : kLeagues)
        if (l.sport == sport)
          sink = sink + game_progress(e, (uint8_t) l.league) + soccer_goals(e, 280555);
      // Again, with favorite players in each sport league.
      for (const auto &l : kLeagues) {
        if (l.sport != sport)
          continue;
        FavPlayer list[kPlayerSlots]{};
        list[0] = make_player((uint8_t) l.league, 4917849, p.team_id, "MESA JR.");
        list[1] = make_player((uint8_t) l.league, 4396909, p.team_id, "NESMITH");
        // Every slot full: soccer props join the watch.
        Prop props[kPropSlots]{};
        for (int i = 0; i < kPropSlots; i++) {
          if (i >= 2)
            list[i] = make_player((uint8_t) l.league, 8000 + i, p.team_id, "FAV");
          props[i].athlete = i == 0 ? 280555 : 8100 + i;
          props[i].league = (uint8_t) l.league;
        }
        const PlayerWatch w = roster_watch(watch_roster(list, props), (uint8_t) l.league);
        GameSnapshot f;
        ChunkReader r5(data, size, chunk);
        if (parse_event(r5, sport, p.team_id, f, p.event_id, &w)) {
          sink = sink + players(prev, f, (uint8_t) l.league) + players(e, f, (uint8_t) l.league);
          sink = sink + render(e, f, now);
        }
      }
      prev = e;
    }
  }
  sink = sink + markets(data, size, chunk, now);
  (void) sink;
  return 0;
}
