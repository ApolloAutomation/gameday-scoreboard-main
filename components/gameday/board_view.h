// The scoreboard's renderer: lays a board's widgets out for its width and
// draws an update (UpdateFields) into them. The widgets themselves are the
// scoreboard page's (firmware/pages/gameday-live.yaml), which binds them to a
// BoardView once (tt_bind_view); the page keeps the leaderboard's rows
// (gd_board), the fireworks and the scripts the result asks for. The
// positions come from board_layout.h, which the host tests check.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL

#include "esphome/components/image/image.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

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
  lv_obj_t *splash{nullptr};
  lv_obj_t *splash_lbl{nullptr};
  lv_obj_t *fx{nullptr};  // the fireworks canvas, behind the splash's words
  const lv_font_t *f8{nullptr}, *f12{nullptr}, *f16{nullptr};
  int16_t w{0};  // the board's width, from board_layout
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

// Places the board's widgets for a board w px wide on a screen h rows high
// (the splash covers all of it).
void board_layout(BoardView &v, int w, int h);
// Draws one update. The text goes through panel_text() first, the splash
// keeping its line break.
DrawResult board_draw(BoardView &v, const UpdateFields &f);

// The second area (split.h): the page's gd_lower, its time, date and ticker
// labels, and a game layout of its own built here under it (game: logos,
// abbreviations, scores, records, the clock, the situation row, a message,
// a splash and the badges an AHL or ECHL team with no small logo shows). No
// pips, diamond or fireworks: baseball's outs go in the situation row, and
// its splash is the plain color flash.
struct AreaView {
  lv_obj_t *root{nullptr};
  lv_obj_t *time{nullptr}, *date{nullptr}, *ticker{nullptr};
  BoardView game;
  image::Image *blank{nullptr};  // a logo still downloading
  int16_t w{0}, h{0};
};

// The area, once the page has bound it; nullptr before.
AreaView *area_view();
// Builds the area's game layout under root and keeps it (in PSRAM); nullptr
// when there is no memory for it. Once only.
AreaView *area_build(lv_obj_t *root, lv_obj_t *time, lv_obj_t *date, lv_obj_t *ticker, const lv_font_t *f8,
                     const lv_font_t *f12, const lv_font_t *f16, image::Image *blank);
// Shows the area at x, y, w by h, or hides it.
void area_layout(AreaView &a, bool on, int x, int y, int w, int h);
// Draws the area: clock, the time, the date and the ticker (lower_half, with
// main_clock the main board's clock card up and main_date its date under
// its time); otherwise f as a game card or a message. Its splash is the
// color flash only.
DrawResult area_draw(AreaView &a, const UpdateFields &f, bool clock, bool main_clock, bool main_date);
void area_logos(AreaView &a, image::Image *team, image::Image *opp);
void area_hide_splash(AreaView &a);

}  // namespace esphome::gameday

#endif  // USE_LVGL
