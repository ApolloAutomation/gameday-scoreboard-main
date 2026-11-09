// The scoreboard's renderer: lays a board's widgets out for its width and
// draws an update (UpdateFields) into them. The widgets themselves are the
// scoreboard page's (firmware/pages/gameday-live.yaml), which binds them to a
// BoardView once (tt_bind_view); the page keeps the leaderboard's rows
// (gd_board), the fireworks and the scripts the result asks for. The
// positions come from board_layout.h, which the host tests check.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL

#include <ctime>

#include "esphome/components/image/image.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

#include "board_layout.h"

namespace esphome::gameday {

struct UpdateFields;

// A board's widgets. Index 0 is the team's side, on the left; 1 the
// opponent's. A nullptr widget is left alone.
struct BoardView {
  lv_obj_t *game{nullptr};     // the game layout
  lv_obj_t *no_game{nullptr};  // the message and the clock card
  lv_obj_t *board{nullptr};    // the leaderboard's container: shown or hidden, the ticker's host
  lv_obj_t *logo[2]{};
  lv_obj_t *badge[2]{};  // the abbreviation badge in a logo's place (badge.h)
  lv_obj_t *badge_lbl[2]{};
  lv_obj_t *abbr[2]{};
  lv_obj_t *score[2]{};
  lv_obj_t *rec[2]{};   // the record, standing or rank under each logo
  lv_obj_t *poss[2]{};  // possession: a gold underline
  lv_obj_t *clock{nullptr};
  lv_obj_t *down{nullptr};  // the situation row: down and distance, a goal, a series
  lv_obj_t *pip[6]{};       // timeouts (football), red cards (soccer): the team's three, then the opponent's
  lv_obj_t *base[3]{};      // first, second, third
  lv_obj_t *out[3]{};
  lv_obj_t *count{nullptr};
  lv_obj_t *outs{nullptr};
  lv_obj_t *ticker{nullptr};
  lv_obj_t *msg{nullptr};
  lv_obj_t *clock_time{nullptr};
  lv_obj_t *clock_sub{nullptr};
  lv_obj_t *clock_ampm{nullptr};  // a Large clock's AM or PM under its digits (one panel), built by board_build
  lv_obj_t *splash{nullptr};
  lv_obj_t *splash_lbl{nullptr};
  lv_obj_t *fx{nullptr};  // the fireworks canvas, behind the splash's words
  // The splash's logos and points (board_layout.h, splash_layout), built by
  // board_build: two tiles, each a logo or its abbreviation badge, and a label.
  lv_obj_t *tile[2]{};
  lv_obj_t *tile_img[2]{};
  lv_obj_t *tile_lbl[2]{};
  lv_obj_t *splash_pts{nullptr};
  const lv_font_t *f8{nullptr}, *f12{nullptr}, *f16{nullptr}, *f24{nullptr}, *f32{nullptr};
  image::Image *blank{nullptr};  // a logo slot with nothing in it
  int16_t w{0};          // the board's width, from board_layout
  int16_t splash_h{64};  // the splash's height: the screen's
  Sizes sizes;           // Clock size and Text size, from board_layout
  bool clock_split{false};  // the clock card's time is its digits, its AM or PM in clock_ampm
  // What each splash tile shows: the side (0 the team, 1 the opponent, -1
  // none) and whether it is a badge waiting for that side's logo.
  int8_t tile_side[2]{-1, -1};
  bool tile_waiting[2]{};
  uint32_t tile_logos{0};  // the splash's update's two logos, hashed
};

// What board_draw leaves to the page: the scripts and the fireworks are its.
struct DrawResult {
  bool ticker_changed{false};  // the ticker has a new line: time its scroll (tt_ticker_timing)
  bool clock_card{false};      // the clock card is up: fill its time (gd_clock_tick)
  bool clock_date{false};      // its second line is the date (gd_clock_date)
  bool splash{false};          // a splash went up: hide it later (tt_hide_splash)
  bool big_splash{false};      // with fireworks: run them, else stop them
};

// The page's board, once the page has bound it; nullptr before.
BoardView *board_view();
// Keeps the page's board (a copy of bound, in PSRAM) and returns it; nullptr
// when there is no memory for it.
BoardView *board_view_set(const BoardView &bound);
// Builds the widgets the page leaves to the renderer: the splash's logo
// tiles and points under v.splash, and the clock card's AM or PM under
// v.no_game when the board has a clock card. Once.
void board_build(BoardView &v);

// Places the board's widgets for a board w px wide and h rows high (64), its
// splash splash_h rows high (all of a 2x2's four panels), at the owner's
// sizes.
void board_layout(BoardView &v, int w, int h, int splash_h, Sizes sizes);
// Draws one update. The text goes through panel_text() first, the splash
// keeping its line break. team_logo and opp_logo: the cached logos for a
// splash's tiles, nullptr while one downloads (its badge shows).
DrawResult board_draw(BoardView &v, const UpdateFields &f, image::Image *team_logo = nullptr,
                      image::Image *opp_logo = nullptr);
// A logo that came in while the splash shows its badge takes the badge's
// place.
void splash_logos(BoardView &v, image::Image *team_logo, image::Image *opp_logo);
// The clock's time and date into the clock card and the second area's
// clock, where either is up: once a second (gd_clock_tick). card_date: the
// card's second line is the date.
void clock_tick(const struct tm &t, bool card_date);

// The second area (split.h): the page's gd_lower, its time, date and ticker
// labels, and a game layout of its own built here under it (game: logos,
// abbreviations, scores, records, the clock, the situation row, a message,
// a splash and the badges an AHL or ECHL team with no small logo shows). No
// pips, diamond or fireworks: baseball's outs go in the situation row, and
// its splash is the plain color flash.
struct AreaView {
  lv_obj_t *root{nullptr};
  lv_obj_t *time{nullptr}, *date{nullptr}, *ticker{nullptr};
  lv_obj_t *ampm{nullptr};  // a Large clock's AM or PM under its digits (64 wide)
  BoardView game;
  image::Image *blank{nullptr};  // a logo still downloading
  int16_t w{0}, h{0};
  bool time_split{false};  // the time is its digits, its AM or PM in ampm
};

// The area, once the page has bound it; nullptr before.
AreaView *area_view();
// Builds the area's game layout under root and keeps it (in PSRAM); nullptr
// when there is no memory for it. Once only. fonts: the page's board, for
// its fonts.
AreaView *area_build(lv_obj_t *root, lv_obj_t *time, lv_obj_t *date, lv_obj_t *ticker, const BoardView &fonts,
                     image::Image *blank);
// Shows the area at x, y, w by h, or hides it, at the owner's sizes.
void area_layout(AreaView &a, bool on, int x, int y, int w, int h, Sizes sizes);
// Draws the area: clock, the time, the date and the ticker (lower_half, with
// main_clock the main board's clock card up and main_date its date under
// its time); otherwise f as a game card or a message. Its splash is the
// color flash only, with its logo and points.
DrawResult area_draw(AreaView &a, const UpdateFields &f, bool clock, bool main_clock, bool main_date,
                     image::Image *team_logo = nullptr, image::Image *opp_logo = nullptr);
void area_logos(AreaView &a, image::Image *team, image::Image *opp);
void area_hide_splash(AreaView &a);

}  // namespace esphome::gameday

#endif  // USE_LVGL
