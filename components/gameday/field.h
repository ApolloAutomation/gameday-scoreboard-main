// Football's field view: model, picture and frames; pure
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "apps.h"
#include "espn_parse.h"
#include "px565.h"

namespace espn {

// Left team defends the left end zone; yards from there
struct FieldModel {
  uint32_t color[2]{};  // clear of the turf and of black
  int16_t score[2]{};
  uint8_t on{0};    // football in progress, its situation read
  uint8_t spot{0};  // the ball's spot is known
  uint8_t brk{0};   // a break: the line score shows
  uint8_t goal{0};  // goal to go
  uint8_t red{0};   // inside the 20 it attacks
  int8_t side{-1};  // with the ball: 0 left (attacks right), 1 right
  int8_t ball{-1};
  int8_t gain{-1};   // the line to gain; -1 none
  int8_t drive{-1};  // the drive's start; -1 none
  int8_t play0{-1}, play1{-1};  // the last play; -1 none
  int8_t down{0}, dist{0};
  int8_t win{-1};  // the left team's chance, percent
  uint8_t periods{0};
  uint8_t line[2][5]{};
  char abbr[2][6]{};
  uint8_t spare[3]{};
};

// A canvas over the rows above the ticker
struct FieldPic {
  FieldModel m;
  int16_t x{0}, y{0}, w{0}, h{0};      // the canvas, on the board
  int16_t fx{0}, fy{0}, fw{0}, fh{0};  // the field, on the canvas
  int16_t pip_y{-1};                   // timeouts' row; -1 none
  int16_t pip_x[2]{};                  // left's first, right's last
  int8_t to[2]{-1, -1};                // timeouts left; -1 unknown
  int16_t poss_x{0}, poss_y{-1}, poss_w{0}, poss_h{2};  // the gold bar; -1 none
  int16_t win_y{-1};                   // the win bar's row; -1 none
  uint8_t numbers{0};                  // yard numbers where they fit
  uint8_t spare{0};
};
static_assert(std::has_unique_object_representations_v<FieldPic>, "FieldPic compares as bytes");

bool field_same(const FieldPic &a, const FieldPic &b);

// Football in progress with its situation read
inline bool field_ready(const GameSnapshot &g) {
  return g.valid && g.sport == Sport::FOOTBALL && g.state == GameState::IN && g.fb.yard_line >= 0;
}
FieldModel field_model(const GameSnapshot &g);
// ESPN's yard line, from the left goal; -1 off
int field_left(const GameSnapshot &g, int yard_line);
// The canvas x of yards from the left goal line
int field_x(const FieldPic &p, int yards);
// Clear of the turf and of black
uint32_t field_color(uint32_t team);

// The whole canvas, pic.w by pic.h
void field_paint(Px565 &p, const FieldPic &pic);

// "2nd & 5", "3rd & Goal"; short: "2&5", "3&G"
std::string field_down(const FieldModel &m, bool short_form);
// "BUF 6", "50"; short: "BUF6"
std::string field_spot(const GameSnapshot &g, bool short_form);
// "7 plays, 38 yards, 1:36"; short: "7P 38Y 1:36"
std::string field_drive(const Football &f, bool short_form);
// "BUF 71%", the side favored; empty when unknown
std::string field_win(const GameSnapshot &g);

// Labels over the picture
struct SportFrame {
  std::vector<AppText> texts;
  FieldPic pic;
};
// The main board's Field view, w by kBoardRows
void sport_frame(const GameSnapshot &g, const std::string &clock, int w, bool large, SportFrame &out);
// The second area's Game details, w by kBoardRows
void detail_frame(const GameSnapshot &g, const std::string &clock, int w, bool large, SportFrame &out);
// Last play, leaders, the drive, the chance to win
std::string field_ticker(const GameSnapshot &g, bool last_play);

// The Try it shows the sample this long
constexpr uint32_t kFieldTryMs = 15000;
// The Try it's game, in the given teams' colors
GameSnapshot field_sample(const std::string &team, uint32_t team_rgb, const std::string &opp, uint32_t opp_rgb);

namespace detail {
// The board's own game reads only (parse_event)
void fill_field_filter(JsonObject ev);
void field_from_event(JsonObjectConst ev, GameSnapshot &s);
// "7 plays, 38 yards, 1:36" into f
void drive_numbers(const std::string &desc, Football &f);
}  // namespace detail

}  // namespace espn
