// Board renderer; tested positions live in board_layout.h
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL

#include <ctime>

#include "esphome/components/image/image.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

#include "apps.h"
#include "board_layout.h"

namespace esphome::gameday {

struct UpdateFields;

// Index 0 is the team, 1 the opponent; nullptr skipped
struct BoardView {
  lv_obj_t *game{nullptr};
  lv_obj_t *no_game{nullptr};  // the message and the clock card
  lv_obj_t *board{nullptr};    // leaderboard container; also hosts the ticker
  lv_obj_t *logo[2]{};
  lv_obj_t *badge[2]{};  // the abbreviation badge in a logo's place (badge.h)
  lv_obj_t *badge_lbl[2]{};
  lv_obj_t *abbr[2]{};
  lv_obj_t *score[2]{};
  lv_obj_t *rec[2]{};   // the record, standing or rank under each logo
  lv_obj_t *poss[2]{};  // possession: a gold underline
  lv_obj_t *clock{nullptr};
  lv_obj_t *down{nullptr};  // the situation row: down and distance, a goal, a series
  lv_obj_t *pip[6]{};       // timeouts or red cards; team's three, then opponent's
  lv_obj_t *base[3]{};      // first, second, third
  lv_obj_t *out[3]{};
  lv_obj_t *count{nullptr};
  lv_obj_t *outs{nullptr};
  lv_obj_t *ticker{nullptr};
  lv_obj_t *msg{nullptr};
  lv_obj_t *clock_time{nullptr};
  lv_obj_t *clock_sub{nullptr};
  lv_obj_t *clock_ampm{nullptr};  // Large clock's AM/PM under digits, one panel only
  lv_obj_t *splash{nullptr};
  lv_obj_t *splash_lbl{nullptr};
  lv_obj_t *fx{nullptr};  // the fireworks canvas; the second area borrows it
  void (*fx_run)(bool){nullptr};  // starts or stops the fireworks (tt_fx)
  // Splash tiles: a logo or badge each, built by board_build
  lv_obj_t *tile[2]{};
  lv_obj_t *tile_img[2]{};
  lv_obj_t *tile_lbl[2]{};
  lv_obj_t *splash_pts{nullptr};
  // Another display's polyline, over the leaderboard; its points
  lv_obj_t *line{nullptr};
  lv_point_precise_t line_pts[::espn::kAppLinePoints]{};
  // Another display's labels, made on first use
  lv_obj_t *text[::espn::kAppTexts]{};
  const lv_font_t *f8{nullptr}, *f12{nullptr}, *f16{nullptr}, *f24{nullptr}, *f32{nullptr};
  image::Image *blank{nullptr};  // a logo slot with nothing in it
  int16_t w{0};          // the board's width, from board_layout
  int16_t splash_h{64};  // the splash's height: the screen's
  Sizes sizes;           // Clock size and Text size, from board_layout
  bool clock_split{false};  // time shows digits only; AM/PM goes in clock_ampm
  bool pts_mask{false};     // the points over fireworks (SplashLayout)
  // Last draw's longer record and sport, for game_clock
  uint8_t clock_rec{0};
  bool clock_baseball{false};
  // Tile's side (-1 none); waiting: badge awaiting the logo
  int8_t tile_side[2]{-1, -1};
  bool tile_waiting[2]{};
  uint32_t tile_logos{0};  // the splash's update's two logos, hashed
};

// Follow-ups board_draw leaves to the page's scripts
struct DrawResult {
  bool ticker_changed{false};  // the ticker has a new line: time its scroll (tt_ticker_timing)
  bool clock_card{false};      // the clock card is up: fill its time (gd_clock_tick)
  bool clock_date{false};      // its second line is the date (gd_clock_date)
  bool splash{false};          // a splash went up: hide it later (tt_hide_splash)
  bool big_splash{false};      // a splash that gets fireworks
  Spot fx{};                   // where they go in the splash
  int16_t fx_h{0};
};

// The bound board, or nullptr before binding
BoardView *board_view();
// Copies bound into PSRAM; nullptr when out of memory
BoardView *board_view_set(const BoardView &bound);
// Builds splash tiles and clock AM/PM widgets, once
void board_build(BoardView &v);
// Whether board_build found LVGL reading the color marker
bool ticker_colors_on();

// splash_h may exceed h: a 2x2 splash covers all four
void board_layout(BoardView &v, int w, int h, int splash_h, Sizes sizes);
// Logos nullptr while downloading; the splash shows badges
DrawResult board_draw(BoardView &v, const UpdateFields &f, image::Image *team_logo = nullptr,
                      image::Image *opp_logo = nullptr);
// Hides the board's splash and its fireworks
void board_hide_splash(BoardView &v);
// Logo, or its badge when missing or badge set
void board_logos(BoardView &v, image::Image *team, image::Image *opp, const bool badge[2]);
// Swaps a splash badge for its newly loaded logo
void splash_logos(BoardView &v, image::Image *team_logo, image::Image *opp_logo);
// card_date: the card's second line shows the date
void clock_tick(const struct tm &t, bool card_date);

// The leaderboard's labels, as gd_board fills the board's
struct RowsView {
  lv_obj_t *title{nullptr}, *status{nullptr};
  lv_obj_t *pos[kRowsCount]{}, *name[kRowsCount]{}, *value[kRowsCount]{}, *detail[kRowsCount]{};
  bool wide{false};
};

// Second area (split.h): no pips or diamond; fireworks when free
struct AreaView {
  lv_obj_t *root{nullptr};
  lv_obj_t *time{nullptr}, *date{nullptr}, *ticker{nullptr};
  lv_obj_t *ampm{nullptr};  // Large clock's AM/PM under its digits (64 wide)
  BoardView game;  // game.board holds rows and game.line
  RowsView rows;
  image::Image *blank{nullptr};  // a logo still downloading
  int16_t w{0}, h{0};
  bool time_split{false};  // time shows digits only; AM/PM goes in ampm
  uint32_t splash_color{0};  // its flash, should the board take the fireworks
};

// The bound area, or nullptr before binding
AreaView *area_view();
// Once only, in PSRAM; nullptr when out of memory
AreaView *area_build(lv_obj_t *root, lv_obj_t *time, lv_obj_t *date, lv_obj_t *ticker, const BoardView &fonts,
                     image::Image *blank);
void area_layout(AreaView &a, bool on, int x, int y, int w, int h, Sizes sizes);
// main_clock, main_date: what the main board's clock shows
DrawResult area_draw(AreaView &a, const UpdateFields &f, bool clock, bool main_clock, bool main_date,
                     image::Image *team_logo = nullptr, image::Image *opp_logo = nullptr);
void area_hide_splash(AreaView &a);
// Hides the card, clock and ticker while off
void area_clear(AreaView &a);

}  // namespace esphome::gameday

#endif  // USE_LVGL
