// The device page's set requests and Live mode's league pick
// (components/gameday/set_request.h). The table in fixtures/set_cases.json is
// the same one tests/web/mock_device.py checks itself against.
#include <ArduinoJson.h>

#include <climits>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "follow.h"
#include "players.h"
#include "set_request.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define SCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string keys(const std::vector<League> &leagues) {
  std::string out;
  for (League l : leagues)
    out += (out.empty() ? "" : ",") + std::string(league_key(l));
  return out;
}

// What the firmware keeps for the settings a set request reaches, and what
// apply_set_'s setters store. layout_ok is the only other refusal.
struct Settings {
  TeamRef team;
  TeamRef splitteam;
  int split{0};
  int clocksize{0}, textsize{0};
  uint8_t team_league{0};
  // rotatesec: Switch every, in seconds.
  int mode{0}, rotatesec{60}, lockon{15}, release{30}, tz{1}, panels{1}, rows{1}, wiring{0}, standing{0};
  bool collide{false}, today{false}, tz_auto{true}, down{true}, count{true}, match{true}, play{true}, odds{true}, opp{true};
  bool bootaddr{true}, autoupd{false}, beta{false}, board{true}, final{false}, follow{false}, alertfav{false};
  bool watch{true}, watchmine{false}, visit{false};
  TeamRef favs[16];
  uint16_t live_mask{0};
  int alerts{(1 << kAlertBits) - 1};
  int watchstay{60};
  // The idle display and the night hours (IdlePrefs' defaults)
  int idle{0}, idledim{0}, nightstart{kNightStartDefault}, nightend{kNightEndDefault},
      nightbri{kNightBrightnessDefault};
  bool night{false};
  Bet bets[kBetSlots]{};
  Prop props[kPropSlots]{};
  FavPlayer players[kPlayerSlots]{};
};
constexpr int kFavMax = 16;
constexpr int kMaxCols = 4;
constexpr int kMaxRows = 2;

TeamRef team_ref(const char *s) {
  TeamRef t;
  parse_team_ref(s == nullptr ? "" : s, t.league, t.id);
  return t;
}

std::string team_str(const TeamRef &t) {
  return t.id == 0 ? "" : std::string(league_key((League) t.league)) + ":" + std::to_string(t.id);
}

void load(Settings &s, JsonObjectConst o) {
  if (o["team_league"].is<const char *>()) {
    const LeagueInfo *info = league_by_key(o["team_league"]);
    s.team_league = info != nullptr ? (uint8_t) info->league : 0;
  }
  if (o["team"].is<const char *>()) {
    s.team = team_ref(o["team"]);
    if (s.team.id != 0)
      s.team_league = s.team.league;
  }
  if (o["splitteam"].is<const char *>())
    s.splitteam = team_ref(o["splitteam"]);
  if (o["favs"].is<JsonArrayConst>()) {
    int i = 0;
    for (auto f : o["favs"].as<JsonArrayConst>())
      if (i < kFavMax)
        s.favs[i++] = team_ref(f.as<const char *>());
    for (; i < kFavMax; i++)
      s.favs[i] = TeamRef{};
  }
  if (o["live_auto"].is<bool>() || o["live"].is<JsonArrayConst>()) {
    std::string list;
    for (auto l : o["live"].as<JsonArrayConst>())
      list += std::string(l.as<const char *>()) + ",";
    s.live_mask = o["live_auto"].as<bool>() ? 0 : live_mask_from_keys(list);
  }
  auto num = [&](const char *k, int &v) {
    if (o[k].is<int>())
      v = o[k].as<int>();
  };
  auto flag = [&](const char *k, bool &v) {
    if (o[k].is<bool>())
      v = o[k].as<bool>();
  };
  num("mode", s.mode);
  // rotate is minutes; rotatesec beside it wins.
  if (o["rotate"].is<int>())
    s.rotatesec = o["rotate"].as<int>() * 60;
  num("rotatesec", s.rotatesec);
  num("lockon", s.lockon);
  num("release", s.release);
  num("tz", s.tz);
  num("panels", s.panels);
  num("rows", s.rows);
  num("wiring", s.wiring);
  num("alerts", s.alerts);
  num("standing", s.standing);
  num("watchstay", s.watchstay);
  num("split", s.split);
  num("clocksize", s.clocksize);
  num("textsize", s.textsize);
  int collide = s.collide ? 1 : 0;
  num("collide", collide);
  s.collide = collide != 0;
  flag("today", s.today);
  flag("tz_auto", s.tz_auto);
  flag("down", s.down);
  flag("count", s.count);
  flag("match", s.match);
  flag("play", s.play);
  flag("odds", s.odds);
  flag("opp", s.opp);
  flag("bootaddr", s.bootaddr);
  flag("autoupd", s.autoupd);
  flag("beta", s.beta);
  flag("board", s.board);
  flag("final", s.final);
  flag("follow", s.follow);
  flag("visit", s.visit);
  flag("alertfav", s.alertfav);
  num("idle", s.idle);
  num("idledim", s.idledim);
  num("nightbri", s.nightbri);
  flag("night", s.night);
  if (o["nightstart"].is<const char *>())
    s.nightstart = parse_hhmm(o["nightstart"].as<const char *>());
  if (o["nightend"].is<const char *>())
    s.nightend = parse_hhmm(o["nightend"].as<const char *>());
  if (o["bets"].is<JsonArrayConst>()) {
    for (auto &b : s.bets)
      b = Bet{};
    for (JsonObjectConst j : o["bets"].as<JsonArrayConst>()) {
      std::string type = j["type"] | "";
      std::string value = std::to_string(j["slot"] | 0) + "," + (j["l"] | "") + "," +
                          std::to_string(j["id"] | 0u) + "," + type + ",";
      if (type != "ml")
        value += bet_points(j["half"] | 0, type == "spread");
      uint8_t slot = 0;
      Bet b{};
      if (parse_bet(value, slot, b) && slot >= 1)
        s.bets[slot - 1] = b;
      else
        printf("FAIL set_cases.json: a start bet doesn't parse: %s\n", value.c_str());
    }
  }
  if (o["props"].is<JsonArrayConst>()) {
    for (auto &p : s.props)
      p = Prop{};
    for (JsonObjectConst j : o["props"].as<JsonArrayConst>()) {
      std::string side = j["side"] | "";
      std::string value = std::to_string(j["slot"] | 0) + "," + (j["l"] | "") + "," + std::to_string(j["id"] | 0u) +
                          "," + std::to_string(j["team"] | 0u) + "," + (j["name"] | "") + "," + (j["stat"] | "") +
                          "," + side + ",";
      if (side != "yes")
        value += bet_points(j["half"] | 0, false);
      uint8_t slot = 0;
      Prop p{};
      if (parse_prop(value, slot, p) && slot >= 1)
        s.props[slot - 1] = p;
      else
        printf("FAIL set_cases.json: a start prop doesn't parse: %s\n", value.c_str());
    }
  }
  flag("watch", s.watch);
  flag("watchmine", s.watchmine);
  if (o["players"].is<JsonArrayConst>()) {
    for (auto &p : s.players)
      p = FavPlayer{};
    for (JsonObjectConst j : o["players"].as<JsonArrayConst>()) {
      std::string value = std::string(j["l"] | "") + "," + std::to_string(j["id"] | 0u) + "," +
                          std::to_string(j["team"] | 0u) + "," + (j["name"] | "");
      SetOp op;
      if (parse_set("player", value, op, kFavMax))
        player_put(s.players, make_player(op.team.league, op.athlete, op.team.id, op.name));
      else
        printf("FAIL set_cases.json: a start player doesn't parse: %s\n", value.c_str());
    }
  }
}

bool any_favorite(const Settings &s) {
  for (const auto &f : s.favs)
    if (f.id != 0)
      return true;
  return false;
}

// Mirrors gameday.cpp's setters; host tests don't build it.
void apply(Settings &s, const SetOp &op) {
  switch (op.kind) {
    case SetKind::TEAM:
      if (!(op.team == s.team))
        s.follow = s.visit = false;  // reset_game_ drops the visit too
      s.team = op.team;
      if (op.team.id != 0)
        s.team_league = op.team.league;
      break;
    case SetKind::MODE:
      if (op.value != s.mode)
        s.follow = s.visit = false;
      s.mode = op.value;
      break;
    case SetKind::ROTATE: s.rotatesec = op.value; break;
    case SetKind::LOCKON: s.lockon = op.value; break;
    case SetKind::RELEASE: s.release = op.value; break;
    case SetKind::COLLIDE: s.collide = op.on; break;
    case SetKind::TODAY: s.today = op.on; break;
    case SetKind::LIVE:
      if (op.mask != s.live_mask)
        s.follow = false;
      s.live_mask = (uint16_t) op.mask;
      break;
    case SetKind::FAVS:
      for (int i = 0; i < kFavMax; i++)
        s.favs[i] = i < (int) op.teams.size() ? op.teams[i] : TeamRef{};
      s.follow = false;
      break;
    case SetKind::FAV_SLOT:
      s.favs[op.slot - 1] = op.team;
      s.follow = false;
      break;
    case SetKind::TZ: s.tz = op.value; break;
    case SetKind::TZ_AUTO: s.tz_auto = op.on; break;
    case SetKind::DOWN: s.down = op.on; break;
    case SetKind::COUNT: s.count = op.on; break;
    case SetKind::MATCH: s.match = op.on; break;
    case SetKind::PLAY: s.play = op.on; break;
    case SetKind::ODDS: s.odds = op.on; break;
    case SetKind::OPP: s.opp = op.on; break;
    case SetKind::BOOTADDR: s.bootaddr = op.on; break;
    case SetKind::AUTOUPD: s.autoupd = op.on; break;
    case SetKind::BETA: s.beta = op.on; break;
    case SetKind::FOLLOW: {
      // set_follow: nothing to follow leaves the pin alone.
      if (!op.on) {
        s.follow = false;
        break;
      }
      GameSnapshot g;
      g.valid = true;
      g.event_id = "401772001";
      g.state = s.final ? GameState::POST : GameState::IN;
      FollowPin p;
      if (s.follow) {
        p.on = true;
        p.event_id = g.event_id;
      }
      follow_start(p, follow_mode_ok((uint8_t) s.mode, any_favorite(s)), s.board, g, 0);
      s.follow = p.on;
      if (s.follow)
        s.visit = false;  // back to the followed game (end_visit_)
      break;
    }
    case SetKind::ALERTS: s.alerts = (int) op.mask; break;
    case SetKind::ALERTFAV: s.alertfav = op.on; break;
    case SetKind::STANDING: s.standing = op.value; break;
    case SetKind::BET: bet_put(s.bets, op.slot, op.bet); break;  // as set_bet_ does
    case SetKind::BET_DEL: s.bets[op.slot - 1] = Bet{}; break;
    case SetKind::PROP: prop_put(s.props, op.slot, op.prop); break;  // as set_prop_ does
    case SetKind::PROP_DEL: s.props[op.slot - 1] = Prop{}; break;
    case SetKind::WATCH:
      s.watch = op.on;
      if (!op.on)
        s.visit = false;  // set_watch ends the visit
      break;
    case SetKind::WATCHSTAY: s.watchstay = op.value; break;
    case SetKind::WATCHMINE: s.watchmine = op.on; break;
    case SetKind::PLAYER:  // set_player_: nothing changes when every slot is in use
      player_put(s.players, make_player(op.team.league, op.athlete, op.team.id, op.name));
      break;
    case SetKind::PLAYER_DEL: player_drop(s.players, op.team.league, op.athlete); break;
    case SetKind::PANELS: {
      // apply_set_ hands PanelLayout::set_layout a byte each.
      const uint8_t cols = (uint8_t) op.value, rows = (uint8_t) op.rows;
      if (panel_layouts::layout_ok(cols, rows, kMaxCols, kMaxRows)) {
        s.panels = cols;
        s.rows = rows;
      }
      break;
    }
    case SetKind::WIRING: s.wiring = op.value; break;
    case SetKind::SPLIT: s.split = op.value; break;
    case SetKind::CLOCK_SIZE: s.clocksize = op.value; break;
    case SetKind::TEXT_SIZE: s.textsize = op.value; break;
    case SetKind::SPLIT_TEAM:  // set_split_team: never the panel's own team
      if (op.team.id == 0 || !(op.team == s.team))
        s.splitteam = op.team;
      break;
    case SetKind::IDLE: s.idle = op.value; break;
    case SetKind::IDLE_DIM: s.idledim = op.value; break;
    case SetKind::NIGHT: s.night = op.on; break;
    case SetKind::NIGHT_START: s.nightstart = op.value; break;
    case SetKind::NIGHT_END: s.nightend = op.value; break;
    case SetKind::NIGHT_BRI: s.nightbri = op.value; break;
  }
}

// The settings keys of rebuild_state_'s document, teams as league:id.
void state_doc(const Settings &s, JsonDocument &doc) {
  doc["team"] = team_str(s.team);
  doc["mode"] = s.mode;
  doc["rotate"] = rotate_minutes_of(s.rotatesec);
  doc["rotatesec"] = s.rotatesec;
  doc["lockon"] = s.lockon;
  doc["release"] = s.release;
  doc["collide"] = s.collide ? 1 : 0;
  int fav_n = 4;
  for (int i = 4; i < kFavMax; i++)
    if (s.favs[i].id != 0)
      fav_n = i + 1;
  JsonArray favs = doc["favs"].to<JsonArray>();
  for (int i = 0; i < fav_n; i++)
    favs.add(team_str(s.favs[i]));
  doc["today"] = s.today;
  std::vector<uint8_t> fav_leagues;
  for (const auto &f : s.favs)
    if (f.id != 0)
      fav_leagues.push_back(f.league);
  JsonArray live = doc["live"].to<JsonArray>();
  for (League l : live_leagues(s.live_mask, s.team_league, fav_leagues))
    live.add(league_key(l));
  doc["live_auto"] = s.live_mask == 0;
  doc["tz"] = s.tz;
  doc["tz_auto"] = s.tz_auto;
  doc["down"] = s.down;
  doc["count"] = s.count;
  doc["match"] = s.match;
  doc["play"] = s.play;
  doc["odds"] = s.odds;
  doc["opp"] = s.opp;
  doc["bootaddr"] = s.bootaddr;
  doc["autoupd"] = s.autoupd;
  doc["beta"] = s.beta;
  doc["panels"] = s.panels;
  doc["rows"] = s.rows;
  doc["wiring"] = s.wiring;
  doc["follow"] = s.follow;
  doc["visit"] = s.visit;
  doc["alerts"] = s.alerts;
  doc["alertfav"] = s.alertfav;
  doc["standing"] = s.standing;
  doc["idle"] = s.idle;
  doc["idledim"] = s.idledim;
  doc["night"] = s.night;
  doc["nightstart"] = clock_hhmm(s.nightstart);
  doc["nightend"] = clock_hhmm(s.nightend);
  doc["nightbri"] = s.nightbri;
  JsonArray bets = doc["bets"].to<JsonArray>();
  for (int i = 0; i < kBetSlots; i++) {
    const Bet &b = s.bets[i];
    if (bet_empty(b))
      continue;
    JsonObject o = bets.add<JsonObject>();
    o["slot"] = i + 1;
    o["l"] = league_key((League) b.league);
    o["id"] = b.team_id;
    o["type"] = bet_type_key(b);
    o["half"] = b.line;
  }
  JsonArray props = doc["props"].to<JsonArray>();
  for (int i = 0; i < kPropSlots; i++) {
    const Prop &p = s.props[i];
    if (prop_empty(p))
      continue;
    JsonObject o = props.add<JsonObject>();
    o["slot"] = i + 1;
    o["l"] = league_key((League) p.league);
    o["id"] = p.athlete;
    o["team"] = p.team_id;
    o["name"] = std::string(p.name, strnlen(p.name, sizeof(p.name)));
    o["stat"] = kPropStats[p.stat].key;
    o["side"] = prop_side_key(p);
    o["half"] = p.line;
  }
  doc["watch"] = s.watch;
  doc["watchstay"] = s.watchstay;
  doc["watchmine"] = s.watchmine;
  doc["split"] = s.split;
  doc["clocksize"] = s.clocksize;
  doc["textsize"] = s.textsize;
  doc["splitteam"] = team_str(s.splitteam);
  JsonArray players = doc["players"].to<JsonArray>();
  for (const FavPlayer &p : s.players) {
    if (player_empty(p))
      continue;
    JsonObject o = players.add<JsonObject>();
    o["l"] = league_key((League) p.league);
    o["id"] = p.athlete;
    o["team"] = p.team_id;
    o["name"] = (const char *) p.name;
  }
}

void test_fixture_cases() {
  JsonDocument fx;
  DeserializationError err = deserializeJson(fx, slurp("fixtures/set_cases.json"));
  SCHECK(!err, "fixtures/set_cases.json: %s", err.c_str());
  int n = 0;
  for (JsonObjectConst c : fx["cases"].as<JsonArrayConst>()) {
    n++;
    const char *name = c["name"] | "?";
    Settings s;
    load(s, fx["base"].as<JsonObjectConst>());
    load(s, c["start"].as<JsonObjectConst>());
    // handle_set_: only kSetKeys, in that order; 400 when none are there.
    std::vector<std::pair<std::string, std::string>> kv;
    for (const char *key : kSetKeys)
      for (JsonArrayConst p : c["set"].as<JsonArrayConst>())
        if (std::string(p[0].as<const char *>()) == key)
          kv.emplace_back(key, p[1].as<const char *>());
    int status = kv.empty() ? 400 : 200;
    SCHECK(status == (c["status"] | 200), "%s: status %d", name, status);
    SetOp op;
    for (const auto &p : kv)
      if (parse_set(p.first, p.second, op, kFavMax))
        apply(s, op);
    JsonDocument got;
    state_doc(s, got);
    for (JsonPairConst e : c["expect"].as<JsonObjectConst>()) {
      std::string want, have;
      serializeJson(e.value(), want);
      serializeJson(got[e.key().c_str()], have);
      SCHECK(want == have, "%s: %s is %s, expected %s", name, e.key().c_str(), have.c_str(), want.c_str());
    }
  }
  SCHECK(n >= 50, "only %d cases read from fixtures/set_cases.json", n);
}

void test_parse_set() {
  SetOp op;
  SCHECK(!parse_set("bogus", "1", op, kFavMax), "unknown key");
  SCHECK(!parse_set("fav5", "nfl:6", op, kFavMax), "fav5 is not a remote slot");
  SCHECK(!parse_set("fav0", "nfl:6", op, kFavMax), "fav0 is not a remote slot");
  SCHECK(parse_set("fav4", "nfl:6", op, kFavMax) && op.kind == SetKind::FAV_SLOT && op.slot == 4 && op.team.id == 6,
         "fav4");
  SCHECK(parse_set("fav1", "none", op, kFavMax) && op.slot == 1 && op.team.id == 0, "fav1 none");
  SCHECK(!parse_set("fav1", "nfl:99999", op, kFavMax), "fav1 unknown team");
  SCHECK(!parse_set("fav1", "pga:1106", op, kFavMax), "fav1 refuses a golf tour");
  SCHECK(parse_set("team", "lpga:1107", op, kFavMax) && op.team.league == (uint8_t) League::LPGA && op.team.id == 1107,
         "a golf tour is a team");
  SCHECK(!parse_set("fav1", "f1:2030", op, kFavMax), "fav1 refuses a racing series");
  SCHECK(parse_set("team", "nascar:2021", op, kFavMax) && op.team.league == (uint8_t) League::NASCAR &&
             op.team.id == 2021,
         "a racing series is a team");
  SCHECK(parse_set("team", "none", op, kFavMax) && op.kind == SetKind::TEAM && op.team.id == 0, "team none");
  SCHECK(parse_set("team", "nfl:6x", op, kFavMax) && op.team.id == 6, "team id reads like strtoul");

  SCHECK(parse_set("favs", "nfl:6,bogus,nfl:6,mlb:15", op, kFavMax), "favs");
  SCHECK(op.teams.size() == 2 && op.refused.size() == 1 && op.refused[0] == "bogus", "favs drops repeats, logs bad");
  SCHECK(parse_set("favs", "nfl:1,nfl:2,nfl:3", op, 2) && op.teams.size() == 2, "favs stops at fav_max");
  SCHECK(parse_set("favs", "pga:1106,nfl:6", op, kFavMax) && op.teams.size() == 1 && op.teams[0].id == 6 &&
             op.refused.size() == 1 && op.refused[0] == "pga:1106",
         "favs refuses a golf tour and logs it");
  SCHECK(parse_set("favs", "f1:2030,nfl:6", op, kFavMax) && op.teams.size() == 1 && op.teams[0].id == 6 &&
             op.refused.size() == 1 && op.refused[0] == "f1:2030",
         "favs refuses a racing series and logs it");

  // ROTATE carries seconds, from rotate's minutes or rotatesec's seconds.
  SCHECK(parse_set("rotate", "-5", op, kFavMax) && op.kind == SetKind::ROTATE && op.value == kRotateMinMinutes * 60,
         "rotate -5");
  SCHECK(parse_set("rotate", "999", op, kFavMax) && op.value == kRotateMaxMinutes * 60, "rotate 999");
  SCHECK(parse_set("rotate", "0", op, kFavMax) && op.value == 60, "rotate 0 is a minute, never 30 seconds");
  SCHECK(parse_set("rotatesec", "30", op, kFavMax) && op.kind == SetKind::ROTATE && op.value == kRotateHalfSeconds,
         "rotatesec 30");
  SCHECK(parse_set("rotatesec", "abc", op, kFavMax) && op.value == kRotateHalfSeconds, "rotatesec text is 30 seconds");
  SCHECK(parse_set("rotatesec", "90", op, kFavMax) && op.value == 60, "rotatesec 90 rounds down");
  SCHECK(parse_set("rotatesec", "1800", op, kFavMax) && op.value == 1800, "rotatesec 1800");
  SCHECK(parse_set("rotatesec", "1801", op, kFavMax) && op.value == 1800, "rotatesec 1801");
  int rotate_at = -1, rotatesec_at = -1;
  for (int i = 0; i < (int) (sizeof(kSetKeys) / sizeof(kSetKeys[0])); i++) {
    if (std::string(kSetKeys[i]) == "rotate")
      rotate_at = i;
    if (std::string(kSetKeys[i]) == "rotatesec")
      rotatesec_at = i;
  }
  SCHECK(rotate_at >= 0 && rotate_at < rotatesec_at, "rotatesec applies after rotate");
  SCHECK(parse_set("lockon", "", op, kFavMax) && op.value == kLockonMinMinutes, "lockon empty");
  SCHECK(parse_set("release", "100000", op, kFavMax) && op.value == kReleaseMaxSeconds, "release 100000");
  SCHECK(parse_set("mode", "0", op, kFavMax) && op.value == 0, "mode 0");
  SCHECK(parse_set("mode", "5", op, kFavMax) && op.value == 5, "mode 5");
  SCHECK(!parse_set("mode", "6", op, kFavMax), "mode 6");
  SCHECK(parse_set("panels", "7", op, kFavMax) && op.value == 7 && op.rows == 1, "panels passes the count through");
  SCHECK(!parse_set("panels", "257", op, kFavMax), "panels 257 would wrap to 1 in PanelLayout's byte");
  SCHECK(!parse_set("panels", "-254", op, kFavMax), "panels -254 would wrap to 2 in PanelLayout's byte");
  SCHECK(!parse_set("panels", "0", op, kFavMax), "panels 0");
  SCHECK(parse_set("panels", "2x2", op, kFavMax) && op.value == 2 && op.rows == 2, "panels 2x2");
  SCHECK(parse_set("panels", "3x7", op, kFavMax) && op.value == 3 && op.rows == 7, "panels passes wide by high through");
  SCHECK(!parse_set("panels", "2x0", op, kFavMax), "panels with no rows");
  SCHECK(!parse_set("panels", "2x", op, kFavMax), "panels with nothing after the x");
  SCHECK(!parse_set("panels", "2x257", op, kFavMax), "panels rows 257 would wrap in PanelLayout's byte");
  SCHECK(!parse_set("panels", "x2", op, kFavMax), "panels with nothing before the x");
  SCHECK(parse_set("wiring", "0", op, kFavMax) && op.kind == SetKind::WIRING && op.value == 0, "wiring 0");
  SCHECK(parse_set("wiring", "2", op, kFavMax) && op.value == panel_layouts::WIRING_TOP_FLIPPED, "wiring 2");
  SCHECK(!parse_set("wiring", "3", op, kFavMax), "wiring past the last");
  SCHECK(!parse_set("wiring", "-1", op, kFavMax), "wiring -1");
  {
    // By name, since earlier keys shift the indexes.
    int panels_at = -1, wiring_at = -1;
    for (int i = 0; i < (int) (sizeof(kSetKeys) / sizeof(kSetKeys[0])); i++) {
      if (std::string(kSetKeys[i]) == "panels")
        panels_at = i;
      if (std::string(kSetKeys[i]) == "wiring")
        wiring_at = i;
    }
    SCHECK(panels_at >= 0 && wiring_at == panels_at + 1, "wiring applies right after panels");
  }

  // The arrangements PanelLayout drives with gameday-common.yaml's limits.
  SCHECK(panel_layouts::layout_ok(1, 1, 4, 2) && panel_layouts::layout_ok(4, 1, 4, 2), "one to four wide");
  SCHECK(!panel_layouts::layout_ok(5, 1, 4, 2) && !panel_layouts::layout_ok(0, 1, 4, 2), "five wide, none wide");
  SCHECK(panel_layouts::layout_ok(2, 2, 4, 2), "the 2x2");
  SCHECK(!panel_layouts::layout_ok(2, 2, 4, 1), "no 2x2 on a build with one row");
  SCHECK(!panel_layouts::layout_ok(2, 2, 3, 2), "no 2x2 on a build that chains three");
  SCHECK(!panel_layouts::layout_ok(1, 2, 4, 2) && !panel_layouts::layout_ok(3, 2, 4, 2), "only the 2x2 has two rows");
  SCHECK(!panel_layouts::layout_ok(2, 3, 8, 3), "no third row");
  SCHECK(panel_layouts::bluetooth_ok(1, 1) && panel_layouts::bluetooth_ok(2, 1), "Bluetooth with one or two panels");
  SCHECK(!panel_layouts::bluetooth_ok(3, 1) && !panel_layouts::bluetooth_ok(4, 1) &&
             !panel_layouts::bluetooth_ok(2, 2),
         "no Bluetooth with three or four");
  SCHECK(parse_set("tz", "America/Chicago", op, kFavMax) && op.value == 1, "tz IANA");
  SCHECK(!parse_set("tz", "-1", op, kFavMax), "tz -1");
  SCHECK(parse_set("down", "1", op, kFavMax) && op.on, "down 1");
  SCHECK(parse_set("down", "on", op, kFavMax) && !op.on, "down on is off");
  SCHECK(parse_set("follow", "1", op, kFavMax) && op.kind == SetKind::FOLLOW && op.on, "follow 1");
  SCHECK(parse_set("follow", "0", op, kFavMax) && op.kind == SetKind::FOLLOW && !op.on, "follow 0");
  SCHECK(parse_set("follow", "", op, kFavMax) && !op.on, "an empty follow lets go");
  SCHECK(std::string(kSetKeys[0]) == "follow", "follow applies before the changes that let it go");
  SCHECK(parse_set("alerts", "0", op, kFavMax) && op.kind == SetKind::ALERTS && op.mask == 0, "alerts 0");
  SCHECK(parse_set("alerts", std::to_string((1 << kAlertBits) - 1), op, kFavMax) && op.mask == (1u << kAlertBits) - 1,
         "alerts all");
  SCHECK(!parse_set("alerts", std::to_string(1 << kAlertBits), op, kFavMax), "alerts past the known types");
  SCHECK(!parse_set("alerts", "-1", op, kFavMax), "alerts -1");
  SCHECK(!parse_set("alerts", "99999999999", op, kFavMax), "alerts past int");
  // Empty must not parse as 0: every switch off.
  SCHECK(!parse_set("alerts", "", op, kFavMax), "alerts empty");
  SCHECK(!parse_set("alerts", "abc", op, kFavMax), "alerts text");
  SCHECK(!parse_set("alerts", " ", op, kFavMax), "alerts space");
  SCHECK(parse_set("alerts", " 12x", op, kFavMax) && op.mask == 12, "alerts after a space");
  SCHECK(parse_set("alertfav", "1", op, kFavMax) && op.kind == SetKind::ALERTFAV && op.on, "alertfav 1");
  SCHECK(parse_set("standing", "2", op, kFavMax) && op.kind == SetKind::STANDING && op.value == (int) Standing::PLACE,
         "standing 2 is place");
  SCHECK(parse_set("standing", "1", op, kFavMax) && op.value == (int) Standing::SEED, "standing 1 is seed");
  SCHECK(!parse_set("standing", "3", op, kFavMax), "standing 3");
  SCHECK(!parse_set("standing", "-1", op, kFavMax), "standing -1");
  SCHECK(parse_set("clocksize", "1", op, kFavMax) && op.kind == SetKind::CLOCK_SIZE && op.value == 1, "clocksize 1");
  SCHECK(parse_set("textsize", "0", op, kFavMax) && op.kind == SetKind::TEXT_SIZE && op.value == 0, "textsize 0");
  for (const char *bad : {"2", "-1", "", "01", "1 ", "L", "large"}) {
    SCHECK(!parse_set("clocksize", bad, op, kFavMax), "clocksize %s", bad);
    SCHECK(!parse_set("textsize", bad, op, kFavMax), "textsize %s", bad);
  }

  SCHECK(parse_set("idle", "4", op, kFavMax) && op.kind == SetKind::IDLE && op.value == 4, "idle 4");
  SCHECK(parse_set("idle", "0", op, kFavMax) && op.value == 0, "idle 0");
  SCHECK(!parse_set("idle", "5", op, kFavMax), "idle 5 is past the choices");
  SCHECK(!parse_set("idle", "-1", op, kFavMax), "idle -1");
  SCHECK(parse_set("idledim", "0", op, kFavMax) && op.kind == SetKind::IDLE_DIM && op.value == 0, "idledim 0 is dark");
  SCHECK(parse_set("idledim", "300", op, kFavMax) && op.value == kIdleDimMax, "idledim clamps down");
  SCHECK(parse_set("idledim", "-4", op, kFavMax) && op.value == 0, "idledim clamps up");
  SCHECK(parse_set("night", "1", op, kFavMax) && op.kind == SetKind::NIGHT && op.on, "night 1");
  SCHECK(parse_set("nightstart", "22:30", op, kFavMax) && op.kind == SetKind::NIGHT_START && op.value == 22 * 60 + 30,
         "nightstart");
  SCHECK(parse_set("nightend", "6:45", op, kFavMax) && op.kind == SetKind::NIGHT_END && op.value == 6 * 60 + 45,
         "nightend");
  SCHECK(!parse_set("nightstart", "1350", op, kFavMax), "nightstart in minutes is refused");
  SCHECK(!parse_set("nightend", "25:00", op, kFavMax), "nightend 25:00");
  SCHECK(parse_set("nightbri", "0", op, kFavMax) && op.kind == SetKind::NIGHT_BRI && op.value == kNightBriMin,
         "nightbri never goes dark");
  SCHECK(parse_set("nightbri", "999", op, kFavMax) && op.value == kNightBriMax, "nightbri clamps down");

  int del_at = -1, bet_at = -1, at = 0;
  for (const char *key : kSetKeys) {
    if (std::string(key) == "betdel")
      del_at = at;
    if (std::string(key) == "bet")
      bet_at = at;
    at++;
  }
  SCHECK(del_at >= 0 && del_at < bet_at, "betdel empties a slot before bet fills it");
  SCHECK(parse_set("watch", "0", op, kFavMax) && op.kind == SetKind::WATCH && !op.on, "watch 0");
  SCHECK(parse_set("watchmine", "1", op, kFavMax) && op.kind == SetKind::WATCHMINE && op.on, "watchmine 1");
  SCHECK(parse_set("watchstay", "120", op, kFavMax) && op.kind == SetKind::WATCHSTAY && op.value == 120,
         "watchstay 120");
  SCHECK(parse_set("watchstay", "5", op, kFavMax) && op.value == kWatchStayMinSeconds, "watchstay floor");
  SCHECK(parse_set("watchstay", "99999999999", op, kFavMax) && op.value == kWatchStayMaxSeconds, "watchstay ceiling");
  SCHECK(parse_set("watchstay", "45", op, kFavMax) && op.value == 40, "watchstay in tens of seconds");
  int pdel_at = -1, player_at = -1;
  at = 0;
  for (const char *key : kSetKeys) {
    if (std::string(key) == "playerdel")
      pdel_at = at;
    if (std::string(key) == "player")
      player_at = at;
    at++;
  }
  SCHECK(pdel_at >= 0 && pdel_at < player_at, "playerdel drops a player before player adds one");

  // Every key handle_set_ forwards decodes to something.
  for (const char *key : kSetKeys) {
    bool known = parse_set(key, "1", op, kFavMax) || parse_set(key, "nfl:6", op, kFavMax) ||
                 parse_set(key, "", op, kFavMax) || parse_set(key, "07:00", op, kFavMax) ||
                 parse_set(key, "1,nfl,6,ml,", op, kFavMax) || parse_set(key, "nfl,3918298,2,ALLEN", op, kFavMax) ||
                 parse_set(key, "nfl,3918298", op, kFavMax) ||
                 parse_set(key, "0,nfl,3918298,2,ALLEN,td,yes,", op, kFavMax);
    SCHECK(known, "kSetKeys has %s but parse_set refuses every value", key);
  }
}

// The edges of each limit, and the team references and lists set_cases.json
// leaves out.
void test_set_limits() {
  SCHECK(clamp_lockon_minutes(4) == kLockonMinMinutes && clamp_lockon_minutes(5) == 5, "lockon floor");
  SCHECK(clamp_lockon_minutes(120) == 120 && clamp_lockon_minutes(121) == kLockonMaxMinutes, "lockon ceiling");
  SCHECK(clamp_lockon_minutes(INT_MIN) == kLockonMinMinutes && clamp_lockon_minutes(INT_MAX) == kLockonMaxMinutes,
         "lockon at the ends of int");
  SCHECK(clamp_release_seconds(29) == kReleaseMinSeconds && clamp_release_seconds(30) == 30, "release floor");
  SCHECK(clamp_release_seconds(3600) == 3600 && clamp_release_seconds(3601) == kReleaseMaxSeconds,
         "release ceiling");
  SCHECK(clamp_release_seconds(INT_MIN) == kReleaseMinSeconds && clamp_release_seconds(INT_MAX) == kReleaseMaxSeconds,
         "release at the ends of int");

  // The limits themselves are kept as sent.
  SetOp op;
  SCHECK(parse_set("lockon", "5", op, kFavMax) && op.value == 5, "lockon 5");
  SCHECK(parse_set("lockon", "120", op, kFavMax) && op.value == 120, "lockon 120");
  SCHECK(parse_set("release", "30", op, kFavMax) && op.value == 30, "release 30");
  SCHECK(parse_set("release", "3600", op, kFavMax) && op.value == 3600, "release 3600");
  SCHECK(parse_set("rotate", "1", op, kFavMax) && op.value == 60, "rotate 1");
  SCHECK(parse_set("rotate", "30", op, kFavMax) && op.value == 1800, "rotate 30");
  SCHECK(parse_set("rotatesec", "30", op, kFavMax) && op.value == 30, "rotatesec 30");
  SCHECK(parse_set("rotatesec", "60", op, kFavMax) && op.value == 60, "rotatesec 60");
  for (int v : {INT_MIN, -1, 0, 29, 30, 31, 59, 60, 61, 119, 120, 1799, 1800, 1801, INT_MAX}) {
    SCHECK(parse_set("rotatesec", std::to_string(v), op, kFavMax) && rotate_seconds_valid(op.value),
           "rotatesec %d is saved as %d", v, op.value);
    SCHECK(parse_set("rotate", std::to_string(v), op, kFavMax) && rotate_seconds_valid(op.value) && op.value >= 60,
           "rotate %d is saved as %d", v, op.value);
  }
  // Numbers read like atoi: leading spaces skipped, trailing text ignored.
  SCHECK(parse_set("mode", " 3", op, kFavMax) && op.value == 3, "mode with a leading space");
  SCHECK(parse_set("mode", "4x", op, kFavMax) && op.value == 4, "mode with trailing text");
  // A zone's position may carry leading zeros; a sign makes it a name, and no zone has that name.
  const std::string last = std::to_string(kTimezoneCount - 1);
  SCHECK(parse_set("tz", "0" + last, op, kFavMax) && (size_t) op.value == kTimezoneCount - 1, "tz 0%s", last.c_str());
  SCHECK(!parse_set("tz", "+1", op, kFavMax), "tz +1");
  SCHECK(parse_set("tz", last, op, kFavMax) && (size_t) op.value == kTimezoneCount - 1, "tz last");
  SCHECK(!parse_set("tz", std::to_string(kTimezoneCount), op, kFavMax), "tz one past the last");

  // Team references: league key, colon, ESPN id.
  uint8_t league = 0;
  uint32_t id = 0;
  SCHECK(parse_team_ref("nfl:6", league, id) && league == (uint8_t) League::NFL && id == 6, "nfl:6");
  SCHECK(parse_team_ref("nhl:25:7", league, id) && league == (uint8_t) League::NHL && id == 25,
         "an id reads up to its first non-digit");
  SCHECK(!parse_team_ref("nfl:", league, id) && id == 0, "no id");
  SCHECK(!parse_team_ref(":6", league, id), "no league");
  SCHECK(!parse_team_ref("NFL:6", league, id) && league == 0, "league keys are lower case");
  SCHECK(!parse_team_ref("nfl6", league, id) && league == 0 && id == 0, "no colon");
  SCHECK(!parse_team_ref("", league, id), "empty");

  // favs: a trailing comma is nothing, an empty item is refused, repeats
  // don't use up a place, and reading stops once the list is full.
  SCHECK(parse_set("favs", "nfl:6,", op, kFavMax) && op.teams.size() == 1 && op.refused.empty(), "trailing comma");
  SCHECK(parse_set("favs", "nfl:6,,mlb:15", op, kFavMax) && op.teams.size() == 2 && op.refused.size() == 1 &&
             op.refused[0].empty(),
         "empty item");
  SCHECK(parse_set("favs", "nfl:1,nfl:1,nfl:2", op, 2) && op.teams.size() == 2 && op.teams[1].id == 2,
         "a repeat doesn't take a place");
  SCHECK(parse_set("favs", "nfl:1,nfl:2,bogus", op, 2) && op.teams.size() == 2 && op.refused.empty(),
         "nothing past a full list is read");
  SCHECK(parse_set("favs", "nfl:1", op, 0) && op.teams.empty(), "no room at all");
}

void test_live_leagues() {
  std::vector<uint8_t> none;
  SCHECK(keys(live_leagues(0, (uint8_t) League::NFL, none)) == "nfl", "auto, nothing saved");
  SCHECK(keys(live_leagues(0, (uint8_t) League::MLB, none)) == "mlb", "auto, team only");
  SCHECK(keys(live_leagues(0, (uint8_t) League::NHL, {(uint8_t) League::NCAA, (uint8_t) League::NHL})) == "ncaa,nhl",
         "auto, team plus favorites, league order");
  SCHECK(keys(live_leagues(0, 200, none)) == "nfl", "auto, unknown league byte falls back to NFL");
  SCHECK(keys(live_leagues(0, 200, {(uint8_t) League::EPL})) == "epl", "auto, unknown team league skipped");
  uint16_t mlb_nba = (uint16_t) ((1u << (uint8_t) League::MLB) | (1u << (uint8_t) League::NBA));
  SCHECK(keys(live_leagues(mlb_nba, (uint8_t) League::NFL, {(uint8_t) League::NHL})) == "mlb,nba",
         "an explicit mask ignores team and favorites");
  SCHECK(keys(live_leagues(1u << 15, (uint8_t) League::MLB, none)) == "echl", "the mask's last bit is the ECHL");
  SCHECK(keys(live_leagues(0, (uint8_t) League::PGA, none)) == "nfl", "auto skips a golf team");
  SCHECK(keys(live_leagues(0, (uint8_t) League::PGA, {(uint8_t) League::LPGA, (uint8_t) League::NHL})) == "nhl",
         "auto skips golf");
  SCHECK(keys(live_leagues(1u << (uint8_t) League::PGA, (uint8_t) League::MLB, none)) == "nfl",
         "a mask of golf alone is NFL");
  SCHECK(live_mask_from_keys("pga,lpga") == 0, "golf keys");
  SCHECK(keys(live_leagues(0, (uint8_t) League::F1, none)) == "nfl", "auto skips a racing team");
  SCHECK(keys(live_leagues(0, (uint8_t) League::NASCAR, {(uint8_t) League::F1, (uint8_t) League::MLB})) == "mlb",
         "auto skips racing");
  SCHECK(keys(live_leagues(1u << (uint8_t) League::F1, (uint8_t) League::MLB, none)) == "nfl",
         "a mask of racing alone is NFL");
  SCHECK(live_mask_from_keys("f1,nascar") == 0, "racing keys");
  SCHECK(live_mask_from_keys("f1,nhl") == (1u << (uint8_t) League::NHL), "racing keys beside a league");

  SCHECK(live_mask_from_keys("") == 0, "empty keys");
  SCHECK(live_mask_from_keys("xfl,MLB") == 0, "unknown and upper-case keys");
  SCHECK(live_mask_from_keys("mlb,,mlb") == (1u << (uint8_t) League::MLB), "repeats and empty items");
  SCHECK(live_mask_from_keys("nhl,") == (1u << (uint8_t) League::NHL), "trailing comma");
}

// What each mode's scan reads is what its finals rotation walks.
void test_scan_leagues() {
  std::vector<League> live = {League::MLB, League::NBA, League::NHL};
  SCHECK(keys(scan_leagues((uint8_t) Mode::MY_TEAM, live)) == "", "My team scans none");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_NFL, live)) == "nfl", "Live NFL");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_NCAA, live)) == "ncaa", "Live college");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_ANY, live)) == "nfl,ncaa", "Live anything and the no-team default");
  SCHECK(keys(scan_leagues((uint8_t) Mode::FAVORITES, live)) == "", "Favorites scans none");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, live)) == "mlb,nba,nhl", "Live games, its own leagues");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, live_leagues(0, (uint8_t) League::EPL, {}))) == "epl",
         "Live games, auto from the team's league");
  SCHECK(keys(scan_leagues(kModeCount, live)) == "", "an unknown mode scans none");
}

// A change to Live games' leagues restarts the scan only when it changes what
// the scan reads.
void test_live_scan_changed() {
  const uint8_t live = (uint8_t) Mode::LIVE;
  const uint8_t nfl = (uint8_t) League::NFL;
  std::vector<uint8_t> none;
  uint16_t ncaa = (uint16_t) (1u << (uint8_t) League::NCAA);
  uint16_t nfl_only = (uint16_t) (1u << nfl);
  // No team saved, list on auto (NFL), then set to college only: the
  // living room panel that kept cycling NFL finals.
  SCHECK(live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(ncaa, nfl, none)), "auto NFL to college");
  SCHECK(!live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(nfl_only, nfl, none)),
         "auto NFL to an explicit NFL reads the same league");
  SCHECK(!live_scan_changed(live, live_leagues(ncaa, nfl, none), live_leagues(ncaa, nfl, none)), "same list");
  // On auto a new favorite in another league adds that league.
  SCHECK(live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(0, nfl, {(uint8_t) League::NHL})),
         "auto, a favorite added in a new league");
  SCHECK(!live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(0, nfl, {nfl})),
         "auto, a favorite added in the team's league");
  SCHECK(!live_scan_changed(live, live_leagues(ncaa, nfl, none), live_leagues(ncaa, nfl, {(uint8_t) League::NHL})),
         "an explicit list ignores a new favorite");
  // Modes that don't read the list never restart for it.
  for (uint8_t m : {(uint8_t) Mode::MY_TEAM, (uint8_t) Mode::LIVE_NFL, (uint8_t) Mode::LIVE_NCAA,
                    (uint8_t) Mode::LIVE_ANY, (uint8_t) Mode::FAVORITES})
    SCHECK(!live_scan_changed(m, live_leagues(0, nfl, none), live_leagues(ncaa, nfl, none)),
           "mode %u ignores Live games' list", (unsigned) m);
}

}  // namespace

int run_set_tests(int &total_checks) {
  test_parse_set();
  test_set_limits();
  test_live_leagues();
  test_scan_leagues();
  test_live_scan_changed();
  test_fixture_cases();
  total_checks += checks;
  return failures;
}
