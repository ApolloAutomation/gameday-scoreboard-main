// Field view images for review, from recorded ESPN documents
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "ArduinoJson.h"
#include "field_render.h"
#include "gif_write.h"
#include "idle.h"

namespace espn {

struct FieldCase {
  const char *name, *file;
  int event;  // a day scoreboard's event; -1 a single-event document
  uint32_t team;
};

// Each fixture once from each side where it matters
constexpr FieldCase kFieldCases[] = {
    {"nfl_buf", "event_nfl_live.json", -1, 2},       {"nfl_min", "event_nfl_live.json", -1, 16},
    {"ncaa_fiu", "event_ncaa_live.json", -1, 2229},  {"duke_goal", "scoreboard_ncaa_live.json", 6, 150},
    {"aub_4th", "scoreboard_ncaa_live.json", 4, 2},  {"mrsh_own3", "scoreboard_ncaa_live.json", 2, 276},
    {"app_td", "scoreboard_ncaa_live.json", 11, 2026}, {"clt_half", "scoreboard_ncaa_live.json", 7, 2429},
};

inline bool field_case_game(const std::string &fixtures, const FieldCase &c, GameSnapshot &g) {
  std::ifstream in(fixtures + "/" + c.file, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string json = ss.str();
  if (c.event >= 0) {
    JsonDocument doc;
    if (deserializeJson(doc, json, DeserializationOption::NestingLimit(40)))
      return false;
    json.clear();
    serializeJson(doc["events"][c.event], json);
  }
  return parse_event_str(json, Sport::FOOTBALL, c.team, g);
}

inline bool field_gif(const std::string &path, const Canvas &cv) {
  GifWriter gw;
  gw.w = cv.w;
  gw.h = cv.h;
  gw.scale = 4;
  gw.add(cv.px.data(), cv.w, 100);
  return gw.save(path);
}

// Every board and area width; returns the files written
inline int render_field_views(const std::string &dir, const std::string &fixtures, const ReviewFonts &fonts) {
  int n = 0;
  std::vector<std::pair<std::string, GameSnapshot>> games;
  for (const FieldCase &c : kFieldCases) {
    GameSnapshot g;
    if (field_case_game(fixtures, c, g))
      games.emplace_back(c.name, g);
    else
      fprintf(stderr, "can't read %s\n", c.name);
  }
  games.emplace_back("sample", field_sample("BUF", 0x00338D, "MIA", 0x008E97));
  // Long clocks beside four-letter names
  GameSnapshot g = field_sample("DUKE", 0x003087, "TULN", 0x006747);
  g.team_score = 21;
  g.opp_score = 17;
  g.period = 4;
  g.display_clock = "12:36";
  g.short_detail = "12:36 - 4th";
  g.fb.periods = 4;
  games.emplace_back("long_clock", g);
  g = field_sample("BUF", 0x00338D, "MIA", 0x008E97);
  g.status_name = "STATUS_END_PERIOD";
  g.display_clock = "0:00";
  g.short_detail = "End of 3rd";
  games.emplace_back("end_3rd", g);
  g = field_sample("UCLA", 0x2D68C4, "WASH", 0x4B2E83);
  g.team_score = g.opp_score = 24;
  g.period = 5;
  g.display_clock = "10:00";
  g.short_detail = "10:00 - OT";
  g.fb.periods = 5;
  games.emplace_back("overtime", g);
  for (const auto &[name, g] : games) {
    const std::string clock = board_clock_text(g, false, 0);
    const std::string ticker = field_ticker(g, true);
    for (int large = 0; large < 2; large++) {
      for (int w : {64, 128, 192, 256}) {
        SportFrame fr;
        sport_frame(g, clock, w, large, fr);
        const std::string file = dir + "/field_" + name + "_" + std::to_string(w) + "x64" + (large ? "_large" : "") + ".gif";
        n += field_gif(file, review_frame(w, fr, fonts, ticker, large));
      }
      for (int w : {64, 128}) {
        SportFrame fr;
        detail_frame(g, clock, w, large, fr);
        const std::string file = dir + "/detail_" + name + "_" + std::to_string(w) + "x64" + (large ? "_large" : "") + ".gif";
        n += field_gif(file, review_frame(w, fr, fonts, ticker, large));
      }
    }
  }
  return n;
}

}  // namespace espn
