#include "board_view.h"

#ifdef USE_LVGL

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#include <esp_heap_caps.h>

#include "alerts.h"
#include "badge.h"
#include "board_layout.h"
#include "espn_parse.h"
#include "gameday.h"
#include "panel_text.h"
#include "setup_qr.h"

namespace esphome::gameday {

namespace {

// Views live in PSRAM; only these pointers are static
BoardView *page_view = nullptr;
AreaView *area_ptr = nullptr;

void show(lv_obj_t *o, bool on) {
  if (o == nullptr)
    return;
  if (on)
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void put_pos(lv_obj_t *o, int x, int y) {
  if (o != nullptr)
    lv_obj_set_pos(o, x, y);
}

void put_width(lv_obj_t *o, int w) {
  if (o != nullptr)
    lv_obj_set_width(o, w);
}

void put_size(lv_obj_t *o, int w, int h) {
  if (o != nullptr)
    lv_obj_set_size(o, w, h);
}

void place(lv_obj_t *o, const Spot &s) {
  put_pos(o, s.x, s.y);
  put_width(o, s.w);
}

void put_font(lv_obj_t *o, const lv_font_t *f) {
  if (o != nullptr && f != nullptr)
    lv_obj_set_style_text_font(o, f, 0);
}

const lv_font_t *font_of(const BoardView &v, BoardFont f) {
  const lv_font_t *p = f == BoardFont::S8    ? v.f8
                       : f == BoardFont::S12 ? v.f12
                       : f == BoardFont::S16 ? v.f16
                       : f == BoardFont::S24 ? v.f24
                                             : v.f32;
  return p != nullptr ? p : v.f16;
}

void put_text(lv_obj_t *o, const char *t) {
  if (o != nullptr)
    lv_label_set_text(o, t);
}

// Rewriting a label restarts its scroll
void put_changed(lv_obj_t *o, const char *t) {
  if (o == nullptr)
    return;
  const char *cur = lv_label_get_text(o);
  if (cur == nullptr || strcmp(cur, t) != 0)
    lv_label_set_text(o, t);
}

void put_bg(lv_obj_t *o, uint32_t c) {
  if (o != nullptr)
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}

void put_color(lv_obj_t *o, uint32_t c) {
  if (o != nullptr)
    lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

int char_px(lv_obj_t *lbl) {
  return (int) lv_font_get_glyph_width(lv_obj_get_style_text_font(lbl, LV_PART_MAIN), '0', 0);
}

// Negative dy means the font's line height
void underline(lv_obj_t *lbl, lv_obj_t *bar, bool on, int dy = -1) {
  if (bar == nullptr)
    return;
  if (!on || lbl == nullptr) {
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const lv_font_t *f = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
  const char *txt = lv_label_get_text(lbl);
  lv_point_t sz;
  lv_text_get_size(&sz, txt, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int32_t tw = sz.x;
  if (tw < 6)
    tw = 6;
  int32_t lx = lv_obj_get_x(lbl), lw = lv_obj_get_width(lbl), ly = lv_obj_get_y(lbl);
  lv_obj_set_size(bar, tw, 2);
  lv_obj_set_pos(bar, lx + (lw - tw) / 2, ly + (dy < 0 ? lv_font_get_line_height(f) : dy));
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *plain_obj(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *plain_label(lv_obj_t *parent, const lv_font_t *f, lv_opa_t opa, bool wrap = false) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_remove_style_all(l);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(l, opa, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
  lv_label_set_text(l, "");
  return l;
}

lv_obj_t *plain_bar(lv_obj_t *parent, uint32_t color) {
  lv_obj_t *o = plain_obj(parent);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

void tile_logo(BoardView &v, int i, image::Image *img) {
  put_bg(v.tile[i], 0x000000);
  lv_obj_set_style_border_width(v.tile[i], 0, 0);
  lv_obj_set_style_radius(v.tile[i], 6, 0);
  ::lv_image_set_src(v.tile_img[i], img->get_lv_image_dsc());
  show(v.tile_img[i], true);
  show(v.tile_lbl[i], false);
  v.tile_waiting[i] = false;
}

// Badge until the logo loads; black ring shows on flash
void paint_tile(BoardView &v, int i, int side, const UpdateFields &f, image::Image *img) {
  if (v.tile[i] == nullptr)
    return;
  const bool made_up = side == 0 ? f.team_badge : f.opponent_badge;
  v.tile_side[i] = (int8_t) side;
  if (img != nullptr && !made_up) {
    tile_logo(v, i, img);
    return;
  }
  v.tile_waiting[i] = !made_up;
  const uint32_t fill = side == 0 ? f.team_color : f.opponent_color;
  const uint32_t ring = made_up ? (side == 0 ? f.team_ring : f.opponent_ring) : fill;
  const ::espn::BadgeStyle st = ::espn::badge_style(fill, ring);
  put_bg(v.tile[i], st.fill);
  lv_obj_set_style_radius(v.tile[i], 8, 0);
  lv_obj_set_style_border_width(v.tile[i], 2, 0);
  lv_obj_set_style_border_color(v.tile[i], lv_color_hex(st.has_ring ? st.ring : 0x000000), 0);
  put_text(v.tile_lbl[i], panel_text(side == 0 ? f.team_abbr : f.opponent_abbr).c_str());
  put_color(v.tile_lbl[i], st.dark_text ? 0x000000 : 0xFFFFFF);
  show(v.tile_img[i], false);
  show(v.tile_lbl[i], true);
}

// Hash of both logos, to spot a newer update's
uint32_t logos_key(const UpdateFields &f) { return fnv1_hash(f.team_logo) * 31u + fnv1_hash(f.opponent_logo); }

}  // namespace

BoardView *board_view() { return page_view; }

BoardView *board_view_set(const BoardView &bound) {
  if (page_view == nullptr) {
    void *p = heap_caps_malloc_prefer(sizeof(BoardView), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
    if (p == nullptr)
      return nullptr;
    page_view = new (p) BoardView(bound);
  } else {
    *page_view = bound;
  }
  return page_view;
}

void board_build(BoardView &v) {
  if (v.clock_ampm == nullptr && v.no_game != nullptr && v.clock_time != nullptr) {
    v.clock_ampm = plain_label(v.no_game, v.f8, LV_OPA_COVER);
    lv_obj_add_flag(v.clock_ampm, LV_OBJ_FLAG_HIDDEN);
  }
  if (v.splash == nullptr || v.tile[0] != nullptr)
    return;
  for (int i = 0; i < 2; i++) {
    lv_obj_t *t = plain_obj(v.splash);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *img = lv_image_create(t);
    lv_obj_remove_style_all(img);
    if (v.blank != nullptr)
      ::lv_image_set_src(img, v.blank->get_lv_image_dsc());
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *lbl = plain_label(t, v.f12, LV_OPA_COVER);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    v.tile[i] = t;
    v.tile_img[i] = img;
    v.tile_lbl[i] = lbl;
  }
  v.splash_pts = plain_label(v.splash, v.f16, LV_OPA_COVER);
  lv_obj_add_flag(v.splash_pts, LV_OBJ_FLAG_HIDDEN);
}

void board_layout(BoardView &v, int w, int h, int splash_h, Sizes sizes) {
  v.w = (int16_t) w;
  v.splash_h = (int16_t) splash_h;
  v.sizes = sizes;
  const GameGeom g = game_geom(w, sizes.text);
  const SplashBox sb = splash_box(w, splash_h);
  show(v.ticker, board_ticker(h));
  // Splash covers the whole screen; fireworks stay 128 wide
  put_pos(v.fx, sb.fx_x, sb.band_y);
  put_size(v.game, w, kBoardRows);
  put_size(v.no_game, w, kBoardRows);
  put_size(v.board, w, kBoardRows);
  put_size(v.splash, w, sb.height);
  put_width(v.splash_lbl, w);
  put_width(v.msg, w);
  // Centered so a three-line wrap still fits one panel
  put_font(v.msg, font_of(v, g.msg_font));
  if (v.msg != nullptr)
    lv_obj_align(v.msg, LV_ALIGN_CENTER, 0, 0);
  const ClockCardGeom c = clock_card_geom(w, sizes.clock);
  v.clock_split = c.ampm;
  put_width(v.clock_time, w);
  put_width(v.clock_sub, w);
  put_width(v.clock_ampm, w);
  put_font(v.clock_time, font_of(v, c.time_font));
  put_font(v.clock_sub, font_of(v, c.sub_font));
  put_pos(v.clock_time, 0, c.time_y);
  put_pos(v.clock_sub, 0, c.sub_y);
  put_pos(v.clock_ampm, 0, c.ampm_y);
  if (!c.ampm)
    show(v.clock_ampm, false);
  put_width(v.ticker, w);
  for (int i = 0; i < 2; i++) {
    put_pos(v.logo[i], g.logo_x[i], g.logo_y);
    put_pos(v.badge[i], g.logo_x[i], g.logo_y);
    put_font(v.abbr[i], font_of(v, g.abbr_font));
    put_font(v.score[i], font_of(v, g.score_font));
    place(v.abbr[i], g.abbr[i]);
    place(v.score[i], g.score[i]);
  }
  put_font(v.clock, font_of(v, g.clock_font));
  place(v.clock, g.clock);
  if (g.wide) {
    for (int i = 0; i < 2; i++) {
      put_font(v.rec[i], v.f8);
      place(v.rec[i], g.rec[i]);
    }
    place(v.down, g.down);
    // Hidden until board_draw knows a side's count
    for (int i = 0; i < 6; i++) {
      show(v.pip[i], false);
      put_pos(v.pip[i], g.pip_x[i], g.pip_y);
    }
  } else {
    // No room for these on a single panel
    show(v.down, false);
    show(v.rec[0], false);
    show(v.rec[1], false);
    for (lv_obj_t *p : v.pip)
      show(p, false);
  }
  put_pos(v.ticker, 0, g.ticker_y);
  put_font(v.splash_lbl, font_of(v, g.splash_font));
  put_pos(v.splash_lbl, 0, sb.text_y);
  for (lv_obj_t *t : v.tile)
    show(t, false);
  show(v.splash_pts, false);
  // Baseball's bases and outs, shown during a half inning
  for (int i = 0; i < 3; i++) {
    put_size(v.base[i], g.base_px, g.base_px);
    put_pos(v.base[i], g.base_x[i], g.base_y[i]);
  }
  if (g.wide) {
    put_font(v.count, v.f8);
    place(v.count, g.count);
    put_font(v.outs, v.f8);
    place(v.outs, g.outs);
    for (lv_obj_t *o : v.out)
      show(o, false);
  } else {
    for (int i = 0; i < 3; i++)
      put_pos(v.out[i], g.out_x[i], g.out_y);
    show(v.count, false);
    show(v.outs, false);
  }
}

DrawResult board_draw(BoardView &v, const UpdateFields &f, image::Image *team_logo, image::Image *opp_logo) {
  DrawResult r;
  const int W = v.w;
  const bool wide = W >= 128;
  const GameGeom g = game_geom(W, v.sizes.text);
  const bool live = f.game_state == "IN";
  // panel_text() so missing glyphs never draw as boxes
  const std::string abbr[2] = {panel_text(f.team_abbr), panel_text(f.opponent_abbr)};
  const std::string status = panel_text(f.status_text);
  const std::string clock_text = panel_text(f.clock_text);
  const std::string down = panel_text(f.down_distance);
  const std::string rec[2] = {panel_text(f.team_record), panel_text(f.opponent_record)};
  const std::string message = panel_text(f.message);
  const std::string &situation = f.situation;
  const std::string &next_line = f.next_line;
  const uint32_t color[2] = {f.team_color, f.opponent_color};
  const bool badge[2] = {f.team_badge, f.opponent_badge};
  const uint32_t ring[2] = {f.team_ring, f.opponent_ring};

  // Badges follow the app's rules so panel and phone match
  for (int i = 0; i < 2; i++) {
    const bool none = abbr[i].empty();
    show(v.logo[i], !none && !badge[i]);
    show(v.badge[i], !none && badge[i]);
    // Styled anyway, for a logo that never comes
    if (none)
      continue;
    const ::espn::BadgeStyle st = ::espn::badge_style(color[i], ring[i]);
    if (v.badge[i] != nullptr) {
      lv_obj_set_style_bg_color(v.badge[i], lv_color_hex(st.fill), 0);
      lv_obj_set_style_border_width(v.badge[i], st.has_ring ? 2 : 0, 0);
      lv_obj_set_style_border_color(v.badge[i], lv_color_hex(st.has_ring ? st.ring : st.fill), 0);
    }
    put_changed(v.badge_lbl[i], abbr[i].c_str());
    put_color(v.badge_lbl[i], st.dark_text ? 0x000000 : 0xFFFFFF);
  }
  put_text(v.score[0], std::to_string(f.team_score).c_str());
  put_text(v.score[1], std::to_string(f.opponent_score).c_str());

  // College rank goes in front only where it fits
  const bool clock_up = (live || f.game_state == "PRE") && !clock_text.empty();
  const int clock_px = clock_up && v.clock != nullptr ? (int) clock_text.size() * char_px(v.clock) : 0;
  auto put_rank = [&](lv_obj_t *lbl, uint8_t rank, const std::string &text, bool row) {
    if (lbl == nullptr)
      return;
    const int cw = char_px(lbl);
    const int lw = (int) lv_obj_get_style_width(lbl, LV_PART_MAIN);
    const size_t fit = cw <= 0 || lw <= 0 ? 0 : row ? ::espn::record_row_chars(clock_px, W, lw, cw) : (size_t) (lw / cw);
    lv_label_set_text(lbl, ::espn::rank_label(rank, text, fit).c_str());
  };
  const bool abbr_rank = !wide && !live;
  put_rank(v.abbr[0], abbr_rank ? f.team_rank : 0, abbr[0], false);
  put_rank(v.abbr[1], abbr_rank ? f.opponent_rank : 0, abbr[1], false);
  put_rank(v.rec[0], wide ? f.team_rank : 0, rec[0], true);
  put_rank(v.rec[1], wide ? f.opponent_rank : 0, rec[1], true);

  // Rewriting restarts the scroll; leaderboards fit only spleen_8
  if (v.ticker != nullptr) {
    const lv_font_t *tf = f.board ? v.f8 : font_of(v, g.ticker_font);
    if (tf != nullptr && lv_obj_get_style_text_font(v.ticker, LV_PART_MAIN) != tf) {
      lv_obj_set_style_text_font(v.ticker, tf, 0);
      r.ticker_changed = true;
    }
    const int ty = f.board ? kBoardRows - 8 : g.ticker_y;
    if (lv_obj_get_style_y(v.ticker, LV_PART_MAIN) != ty)
      lv_obj_set_y(v.ticker, ty);
    const char *cur = lv_label_get_text(v.ticker);
    if (cur == nullptr || status != cur) {
      put_text(v.ticker, status.c_str());
      r.ticker_changed = true;
    }
  }

  // Lit pip is a timeout left, dim is used
  const int to[2] = {f.team_timeouts < 0 ? 0 : (f.team_timeouts > 3 ? 3 : f.team_timeouts),
                     f.opponent_timeouts < 0 ? 0 : (f.opponent_timeouts > 3 ? 3 : f.opponent_timeouts)};
  for (int i = 0; i < 3; i++) {
    put_bg(v.pip[i], i < to[0] ? 0xFFD000 : 0x202020);
    put_bg(v.pip[3 + i], i < to[1] ? 0xFFD000 : 0x202020);
  }

  // Possession is an underline; color swaps read as team changes
  put_color(v.abbr[0], lift_color(f.team_color, 0xC0));
  put_color(v.abbr[1], lift_color(f.opponent_color, 0xC0));
  put_color(v.score[0], 0xFFFFFF);
  put_color(v.score[1], 0xFFFFFF);
  const bool under_scores = !wide && live && !clock_text.empty();
  for (int i = 0; i < 2; i++)
    underline(under_scores ? v.score[i] : v.abbr[i], v.poss[i], live && f.possession == i + 1,
              under_scores ? g.score_poss_dy : -1);

  const bool no_game = no_game_screen(f.game_state, f.clock, f.board_card);
  if (no_game) {
    put_changed(v.msg, f.board_card        ? next_line.c_str()
                       : message.empty() ? "No upcoming game"
                                         : message.c_str());
    show(v.game, false);
    show(v.no_game, true);
  } else {
    show(v.game, !f.board);
    show(v.no_game, false);
  }
  show(v.board, f.board);
  // Hidden game layout lends its ticker so it keeps scrolling
  lv_obj_t *host = v.game;
  switch (ticker_host(no_game, f.clock || f.board_card, f.board, f.join_line)) {
    case TickerHost::NO_GAME:
      host = v.no_game;
      break;
    case TickerHost::BOARD:
      host = v.board;
      break;
    case TickerHost::GAME:
      break;
  }
  if (v.ticker != nullptr && host != nullptr && lv_obj_get_parent(v.ticker) != host)
    lv_obj_set_parent(v.ticker, host);
  const bool card_date = clock_card_date(W, !next_line.empty());
  if (f.clock) {
    show(v.msg, false);
    put_color(v.clock_time, f.team_color);
    put_color(v.clock_ampm, f.team_color);
    r.clock_card = true;
    r.clock_date = card_date;
    if (!card_date)
      put_changed(v.clock_sub, next_line.c_str());
    show(v.clock_time, true);
    show(v.clock_sub, true);
    show(v.clock_ampm, v.clock_split);
  } else {
    show(v.msg, true);
    show(v.clock_time, false);
    show(v.clock_sub, false);
    show(v.clock_ampm, false);
  }
  // No room for records on a single panel
  show(v.rec[0], wide);
  show(v.rec[1], wide);
  if (wide && live && !down.empty()) {
    put_text(v.down, down.c_str());
    put_color(v.down, f.is_red_zone ? 0xFF3030 : 0xFFFFFF);
    show(v.down, true);
  } else {
    show(v.down, false);
  }
  // Soccer (sport 4): latest goal, like "52' Saka"
  if (wide && f.sport == 4 && live && !situation.empty()) {
    put_text(v.down, situation.c_str());
    put_color(v.down, 0xFFFFFF);
    show(v.down, true);
  }
  // Baseball (sport 1); highlight means the bases are loaded
  const bool diamond = f.sport == 1 && live && f.outs >= 0;
  for (int i = 0; i < 3; i++) {
    if (!diamond) {
      show(v.base[i], false);
      show(v.out[i], false);
      continue;
    }
    const bool on = (f.bases >> i) & 1;
    put_bg(v.base[i], on ? (f.highlight ? 0xFF3030 : 0xFFD000) : 0x303030);
    show(v.base[i], true);
    put_bg(v.out[i], i < f.outs ? 0xFFD000 : 0x303030);
    if (!wide)
      show(v.out[i], true);
  }
  if (wide && diamond && !situation.empty()) {
    put_text(v.count, situation.c_str());
    show(v.count, true);
  } else {
    show(v.count, false);
  }
  if (wide && diamond) {
    put_text(v.outs, (std::to_string(f.outs) + " OUT").c_str());
    show(v.outs, true);
  } else {
    show(v.outs, false);
  }
  // Basketball (sport 2): playoff series, like "NY lead 2-1"
  if (f.sport == 2 && wide && live && !situation.empty()) {
    put_text(v.down, situation.c_str());
    put_color(v.down, 0xFFFFFF);
    show(v.down, true);
  }
  if (wide) {
    // Unknown count (-1) hides pips instead of showing used
    for (int i = 0; i < 6; i++)
      show(v.pip[i], f.sport == 0 && (i < 3 ? f.team_timeouts : f.opponent_timeouts) >= 0);
    // Soccer red cards; football's next update recolors them
    if (f.sport == 4) {
      for (int i = 0; i < 3; i++) {
        put_bg(v.pip[i], 0xFF2020);
        put_bg(v.pip[3 + i], 0xFF2020);
        if (i < f.team_marks)
          show(v.pip[i], true);
        if (i < f.opp_marks)
          show(v.pip[3 + i], true);
      }
    }
  }
  // Before a game, only the idle countdown sends a clock
  if (clock_up)
    put_text(v.clock, clock_text.c_str());
  show(v.clock, clock_up);
  show(v.abbr[0], wide || !clock_up);
  show(v.abbr[1], wide || !clock_up);

  // The logo names the side, so words omit it
  const std::string splash = panel_text(f.splash_text, true);
  if (!splash.empty()) {
    const int tiles = f.splash_side == 2 ? 2 : f.splash_side >= 0 ? 1 : 0;
    const std::string words =
        tiles == 1 ? panel_text(f.splash_text.substr(std::min<size_t>(f.splash_lead, f.splash_text.size())), true)
                   : splash;
    const SplashLayout L = splash_layout(W, v.splash_h, v.tile[0] != nullptr ? tiles : 0, f.splash_points, words);
    image::Image *const logo[2] = {team_logo, opp_logo};
    for (int i = 0; i < 2; i++) {
      if (!L.logos || i >= L.tiles) {
        show(v.tile[i], false);
        v.tile_waiting[i] = false;
        continue;
      }
      const int side = L.tiles == 2 ? i : f.splash_side;
      paint_tile(v, i, side, f, logo[side]);
      put_pos(v.tile[i], L.tile_x[i], L.tile_y);
      put_size(v.tile[i], L.tile_px, L.tile_px);
      show(v.tile[i], true);
    }
    v.tile_logos = logos_key(f);
    if (L.logos) {
      put_font(v.splash_lbl, font_of(v, L.words_font));
      place(v.splash_lbl, L.words);
      put_text(v.splash_lbl, words.c_str());
    } else {
      put_font(v.splash_lbl, font_of(v, g.splash_font));
      put_pos(v.splash_lbl, 0, splash_box(W, v.splash_h).text_y);
      put_width(v.splash_lbl, W);
      put_text(v.splash_lbl, splash.c_str());
    }
    if (L.points) {
      put_font(v.splash_pts, font_of(v, L.points_font));
      place(v.splash_pts, L.pts);
      put_text(v.splash_pts, (L.points_whole ? f.splash_points : points_number(f.splash_points)).c_str());
    }
    show(v.splash_pts, L.points);
    const bool big = ::espn::splash_fireworks(splash, (::espn::Sport) f.sport);
    const uint32_t ink = big ? lift_color(f.splash_color, 0xE0) : 0xFFFFFF;
    if (big) {
      put_bg(v.splash, 0x000000);
      show(v.fx, true);
    } else {
      show(v.fx, false);
      put_bg(v.splash, f.splash_color);
    }
    put_color(v.splash_lbl, ink);
    put_color(v.splash_pts, ink);
    show(v.splash, true);
    r.splash = true;
    r.big_splash = big;
  } else if (logos_key(f) != v.tile_logos) {
    // Board moved on; a late logo is another game's
    v.tile_waiting[0] = v.tile_waiting[1] = false;
  }
  return r;
}

void board_logos(BoardView &v, image::Image *team, image::Image *opp, const bool badge[2]) {
  image::Image *const img[2] = {team, opp};
  for (int i = 0; i < 2; i++) {
    if (v.logo[i] == nullptr)
      continue;
    image::Image *src = img[i] != nullptr ? img[i] : v.blank;
    if (src != nullptr)
      ::lv_image_set_src(v.logo[i], src->get_lv_image_dsc());
    // A side board_draw left out stays hidden
    if (v.badge[i] == nullptr ||
        (lv_obj_has_flag(v.logo[i], LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(v.badge[i], LV_OBJ_FLAG_HIDDEN)))
      continue;
    const bool use_badge = badge[i] || img[i] == nullptr;
    show(v.logo[i], !use_badge);
    show(v.badge[i], use_badge);
  }
}

void splash_logos(BoardView &v, image::Image *team_logo, image::Image *opp_logo) {
  if (v.splash == nullptr || lv_obj_has_flag(v.splash, LV_OBJ_FLAG_HIDDEN))
    return;
  for (int i = 0; i < 2; i++) {
    image::Image *img = v.tile_side[i] == 0 ? team_logo : v.tile_side[i] == 1 ? opp_logo : nullptr;
    if (v.tile_waiting[i] && img != nullptr && v.tile[i] != nullptr)
      tile_logo(v, i, img);
  }
}

void clock_tick(const struct tm &t, bool card_date) {
  BoardView *v = page_view;
  AreaView *a = area_ptr;
  const bool card = v != nullptr && v->clock_time != nullptr && !lv_obj_has_flag(v->clock_time, LV_OBJ_FLAG_HIDDEN);
  const bool lower = a != nullptr && a->root != nullptr && !lv_obj_has_flag(a->root, LV_OBJ_FLAG_HIDDEN);
  if (!card && !lower)
    return;
  // Rewritten only on change, so once a minute
  const std::string time = ::espn::clock_time_text(t);
  const std::string date = ::espn::clock_date_text(t);
  const std::string ampm = clock_ampm(time);
  const std::string digits = clock_digits(time);
  if (lower) {
    put_changed(a->time, a->time_split ? digits.c_str() : time.c_str());
    put_changed(a->ampm, ampm.c_str());
    put_changed(a->date, date.c_str());
  }
  if (!card)
    return;
  put_changed(v->clock_time, v->clock_split ? digits.c_str() : time.c_str());
  put_changed(v->clock_ampm, ampm.c_str());
  if (card_date)
    put_changed(v->clock_sub, date.c_str());
}

AreaView *area_view() { return area_ptr; }

AreaView *area_build(lv_obj_t *root, lv_obj_t *time, lv_obj_t *date, lv_obj_t *ticker, const BoardView &fonts,
                     image::Image *blank) {
  if (area_ptr != nullptr || root == nullptr)
    return area_ptr;
  void *p = heap_caps_malloc_prefer(sizeof(AreaView), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
  if (p == nullptr)
    return nullptr;
  AreaView *a = new (p) AreaView();
  a->root = root;
  a->time = time;
  a->date = date;
  a->blank = blank;
  BoardView &v = a->game;
  const lv_font_t *f8 = fonts.f8, *f12 = fonts.f12, *f16 = fonts.f16;
  v.f8 = f8;
  v.f12 = f12;
  v.f16 = f16;
  v.f24 = fonts.f24;
  v.f32 = fonts.f32;
  v.blank = blank;
  a->ticker = ticker;
  a->ampm = plain_label(root, f8, LV_OPA_COVER);
  lv_obj_add_flag(a->ampm, LV_OBJ_FLAG_HIDDEN);
  // Created later, so drawn over the time and ticker
  v.game = plain_obj(root);
  v.no_game = plain_obj(root);
  lv_obj_add_flag(v.game, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(v.no_game, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < 2; i++) {
    v.logo[i] = lv_image_create(v.game);
    lv_obj_remove_style_all(v.logo[i]);
    if (blank != nullptr)
      ::lv_image_set_src(v.logo[i], blank->get_lv_image_dsc());
    // Badge for AHL or ECHL teams lacking a small logo
    v.badge[i] = plain_obj(v.game);
    lv_obj_set_size(v.badge[i], 32, 32);
    lv_obj_set_style_radius(v.badge[i], 8, 0);
    lv_obj_set_style_bg_opa(v.badge[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v.badge[i], 2, 0);
    lv_obj_add_flag(v.badge[i], LV_OBJ_FLAG_HIDDEN);
    v.badge_lbl[i] = plain_label(v.badge[i], f12, LV_OPA_COVER);
    lv_obj_center(v.badge_lbl[i]);
    v.abbr[i] = plain_label(v.game, f12, 230);
    v.score[i] = plain_label(v.game, f16, LV_OPA_COVER);
    v.rec[i] = plain_label(v.game, f8, 191);
    v.poss[i] = plain_bar(v.game, 0xFFD000);
  }
  v.clock = plain_label(v.game, f12, LV_OPA_COVER);
  v.down = plain_label(v.game, f8, 191);
  v.msg = plain_label(v.no_game, f12, 230, true);
  v.splash = plain_bar(root, 0x000000);
  v.splash_lbl = plain_label(v.splash, f16, LV_OPA_COVER, true);
  board_build(v);
  area_ptr = a;
  return a;
}

void area_layout(AreaView &a, bool on, int x, int y, int w, int h, Sizes sizes) {
  show(a.root, on);
  if (!on)
    return;
  a.w = (int16_t) w;
  a.h = (int16_t) h;
  lv_obj_set_pos(a.root, x, y);
  lv_obj_set_size(a.root, w, h);
  put_width(a.time, w);
  put_width(a.date, w);
  put_width(a.ampm, w);
  put_width(a.ticker, w);
  board_layout(a.game, w, h, h, sizes);
}

namespace {

// Set only on change: each set restarts the scroll
void put_y(lv_obj_t *o, int y) {
  if (o != nullptr && lv_obj_get_style_y(o, LV_PART_MAIN) != y)
    lv_obj_set_y(o, y);
}

bool put_ticker(lv_obj_t *t, const lv_font_t *font, int y, const std::string &text) {
  if (t == nullptr)
    return false;
  bool changed = false;
  if (font != nullptr && lv_obj_get_style_text_font(t, LV_PART_MAIN) != font) {
    lv_obj_set_style_text_font(t, font, 0);
    changed = true;
  }
  put_y(t, y);
  const char *cur = lv_label_get_text(t);
  if (cur == nullptr || text != cur) {
    lv_label_set_text(t, text.c_str());
    changed = true;
  }
  return changed;
}

}  // namespace

DrawResult area_draw(AreaView &a, const UpdateFields &f, bool clock, bool main_clock, bool main_date,
                     image::Image *team_logo, image::Image *opp_logo) {
  BoardView &v = a.game;
  const std::string line = panel_text(f.status_text);
  DrawResult r;
  if (clock) {
    show(v.game, false);
    show(v.no_game, false);
    const LowerHalf lower = lower_half(main_clock, main_date, a.w, v.sizes);
    a.time_split = lower.ampm;
    show(a.time, lower.time);
    show(a.ampm, lower.time && lower.ampm);
    show(a.date, lower.date);
    put_font(a.time, font_of(v, lower.time_font));
    put_y(a.time, lower.time_y);
    put_y(a.ampm, lower.ampm_y);
    put_y(a.date, lower.date_y);
    r.ticker_changed = put_ticker(a.ticker, v.f16, lower.ticker_y, line);
    r.clock_card = true;
    return r;
  }
  show(a.time, false);
  show(a.ampm, false);
  show(a.date, false);
  r = board_draw(v, f, team_logo, opp_logo);
  const GameGeom g = game_geom(a.w, v.sizes.text);
  r.ticker_changed = put_ticker(a.ticker, font_of(v, g.ticker_font), g.ticker_y, line);
  if (r.splash) {
    put_bg(v.splash, f.splash_color);
    put_color(v.splash_lbl, 0xFFFFFF);
    put_color(v.splash_pts, 0xFFFFFF);
    r.big_splash = false;
  }
  return r;
}

void area_hide_splash(AreaView &a) { show(a.game.splash, false); }

void area_clear(AreaView &a) {
  BoardView &v = a.game;
  show(v.game, false);
  show(v.no_game, false);
  show(v.splash, false);
  show(a.time, false);
  show(a.ampm, false);
  show(a.date, false);
  if (a.ticker != nullptr)
    lv_label_set_text(a.ticker, "");
}

}  // namespace esphome::gameday

#endif  // USE_LVGL
