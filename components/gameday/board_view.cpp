#include "board_view.h"

#ifdef USE_LVGL

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

// The page's board, in PSRAM: only its pointer is static.
BoardView *page_view = nullptr;

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
  return f == BoardFont::S8 ? v.f8 : f == BoardFont::S12 ? v.f12 : v.f16;
}

void put_text(lv_obj_t *o, const char *t) {
  if (o != nullptr)
    lv_label_set_text(o, t);
}

// Only when the text changed: rewriting a label restarts its scroll.
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

// Possession: a gold bar under lbl's text, at least 6 px wide.
void underline(lv_obj_t *lbl, lv_obj_t *bar, bool on) {
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
  lv_obj_set_pos(bar, lx + (lw - tw) / 2, ly + lv_font_get_line_height(f));
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

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

void board_layout(BoardView &v, int w, int h) {
  v.w = (int16_t) w;
  const GameGeom g = game_geom(w);
  const SplashBox sb = splash_box(w, h);
  // The 2x2's bottom half: the time and date, and the ticker in spleen_16,
  // which board_draw and gd_clock_tick fill. The ticker runs there only, not
  // along the board's bottom as well.
  const bool own_ticker = board_ticker(h);
  show(v.ticker, own_ticker);
  if (!own_ticker && v.lower != nullptr) {
    lv_obj_set_pos(v.lower, 0, kBoardRows);
    lv_obj_set_size(v.lower, w, h - kBoardRows);
    for (lv_obj_t *l : {v.lower_time, v.lower_date, v.lower_ticker})
      put_width(l, w);
  }
  show(v.lower, !own_ticker);
  // A splash covers the whole screen, a 2x2's bottom half too, with its
  // words and fireworks in the board's 64 rows (the middle ones of a 2x2)
  // and the 128 wide fireworks canvas in the middle of a wider board.
  put_pos(v.fx, sb.fx_x, sb.band_y);
  put_size(v.game, w, kBoardRows);
  put_size(v.no_game, w, kBoardRows);
  put_size(v.board, w, kBoardRows);
  put_size(v.splash, w, sb.height);
  put_width(v.splash_lbl, w);
  put_width(v.msg, w);
  // Centered both ways, so a message that wraps to three lines on a single
  // panel ("Saturday's / college / scores") still fits.
  put_font(v.msg, font_of(v, g.msg_font));
  if (v.msg != nullptr)
    lv_obj_align(v.msg, LV_ALIGN_CENTER, 0, 0);
  // The clock card: the time, and under it the next game (two lines) on a
  // wide board or the date on a single panel.
  put_width(v.clock_time, w);
  put_width(v.clock_sub, w);
  put_font(v.clock_time, v.f16);
  put_font(v.clock_sub, font_of(v, g.sub_font));
  put_pos(v.clock_time, 0, g.clock_time_y);
  put_pos(v.clock_sub, 0, g.clock_sub_y);
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
    // Timeout pips are football's; board_draw shows them for a side with a
    // known count, so a layout change never shows them used.
    for (int i = 0; i < 6; i++) {
      show(v.pip[i], false);
      put_pos(v.pip[i], g.pip_x[i], g.pip_y);
    }
  } else {
    // No room for records, pips or the situation row on a single panel
    show(v.down, false);
    show(v.rec[0], false);
    show(v.rec[1], false);
    for (lv_obj_t *p : v.pip)
      show(p, false);
  }
  put_pos(v.ticker, 0, g.ticker_y);
  put_font(v.splash_lbl, font_of(v, g.splash_font));
  put_pos(v.splash_lbl, 0, sb.text_y);
  // Baseball's bases and outs, which board_draw shows during a half inning
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

DrawResult board_draw(BoardView &v, const UpdateFields &f) {
  DrawResult r;
  const int W = v.w;
  const bool wide = W >= 128;
  const bool live = f.game_state == "IN";
  // The text through panel_text(), so a character the fonts lack never draws
  // as a box. The situation and the clock card's next game come ready.
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

  // A side with no team yet (just after a team change) shows no logo. A side
  // with no logo to fetch (the demo build's made-up teams) shows its
  // abbreviation badge in the logo's place, drawn by the app's rules
  // (badge.h) so the panel and the phone match.
  for (int i = 0; i < 2; i++) {
    const bool none = abbr[i].empty();
    show(v.logo[i], !none && !badge[i]);
    if (none || !badge[i]) {
      show(v.badge[i], false);
      continue;
    }
    const ::espn::BadgeStyle st = ::espn::badge_style(color[i], ring[i]);
    if (v.badge[i] != nullptr) {
      lv_obj_set_style_bg_color(v.badge[i], lv_color_hex(st.fill), 0);
      lv_obj_set_style_border_width(v.badge[i], st.has_ring ? 2 : 0, 0);
      lv_obj_set_style_border_color(v.badge[i], lv_color_hex(st.has_ring ? st.ring : st.fill), 0);
    }
    put_changed(v.badge_lbl[i], abbr[i].c_str());
    put_color(v.badge_lbl[i], st.dark_text ? 0x000000 : 0xFFFFFF);
    show(v.badge[i], true);
  }
  put_text(v.score[0], std::to_string(f.team_score).c_str());
  put_text(v.score[1], std::to_string(f.opponent_score).c_str());

  // The abbreviations and records, with a college team's rank ("#5") in
  // front where it fits the label in its font (espn::rank_label; the Spleen
  // fonts are fixed width): before the abbreviation on one panel, before and
  // after the game (the clock has that row during play), and before the
  // record under the logo on two or more, kept a pixel clear of the clock
  // between the records while it shows (espn::record_row_chars), the idle
  // display's countdown to a game too (always clear on three or four panels).
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

  // The ticker only when its text changed: rewriting the label restarts the
  // scroll, and pushes arrive every few seconds.
  lv_obj_t *ticker = v.ticker != nullptr ? v.ticker : v.lower_ticker;
  if (ticker != nullptr) {
    const char *cur = lv_label_get_text(ticker);
    if (cur == nullptr || status != cur) {
      put_text(v.ticker, status.c_str());
      put_text(v.lower_ticker, status.c_str());
      r.ticker_changed = true;
    }
  }

  // Timeout pips: lit bar = timeout remaining, dim bar = used
  const int to[2] = {f.team_timeouts < 0 ? 0 : (f.team_timeouts > 3 ? 3 : f.team_timeouts),
                     f.opponent_timeouts < 0 ? 0 : (f.opponent_timeouts > 3 ? 3 : f.opponent_timeouts)};
  for (int i = 0; i < 3; i++) {
    put_bg(v.pip[i], i < to[0] ? 0xFFD000 : 0x202020);
    put_bg(v.pip[3 + i], i < to[1] ? 0xFFD000 : 0x202020);
  }

  // Abbreviations stay in team colors, lifted so a dark one shows;
  // possession is a gold underline under the side with the ball (a color
  // swap read as a team change). On a single panel the names hide during
  // play, so it underlines the score.
  put_color(v.abbr[0], lift_color(f.team_color, 0xC0));
  put_color(v.abbr[1], lift_color(f.opponent_color, 0xC0));
  put_color(v.score[0], 0xFFFFFF);
  put_color(v.score[1], 0xFFFFFF);
  const bool under_scores = !wide && live && !clock_text.empty();
  for (int i = 0; i < 2; i++)
    underline(under_scores ? v.score[i] : v.abbr[i], v.poss[i], live && f.possession == i + 1);

  // The game layout, a leaderboard or the no-game screen
  const bool no_game = no_game_screen(f.game_state, f.clock, f.board_card);
  if (no_game) {
    // The component's words for this screen (the finals rotation's headers
    // and next kickoff, "No live games", a golf event's name and dates),
    // else the default.
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
  // The clock card and a leaderboard's card take the message's place, and the
  // leaderboard the game's: each borrows the ticker from the hidden game
  // layout so it keeps running under it. So does a message while the address
  // after a join leads the ticker (setup_qr.h, ticker_host).
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
  // The clock card has the date under its time on one panel or with no next
  // game, else the next game. A 2x2's bottom half (lower_half): under the
  // clock card, which has the time already, no time, and the date only when
  // the card has the next game in its place.
  const bool card_date = clock_card_date(W, !next_line.empty());
  const LowerHalf lower = lower_half(f.clock, card_date);
  show(v.lower_time, lower.time);
  show(v.lower_date, lower.date);
  if (v.lower_date != nullptr)
    lv_obj_set_y(v.lower_date, lower.date_y);
  if (v.lower_ticker != nullptr)
    lv_obj_set_y(v.lower_ticker, lower.ticker_y);
  if (f.clock) {
    show(v.msg, false);
    put_color(v.clock_time, f.team_color);
    r.clock_card = true;
    r.clock_date = card_date;
    if (!card_date)
      put_changed(v.clock_sub, next_line.c_str());
    show(v.clock_time, true);
    show(v.clock_sub, true);
  } else {
    show(v.msg, true);
    show(v.clock_time, false);
    show(v.clock_sub, false);
  }
  // Records sit under the logos on a wide board, live or not; a single
  // panel has no room for them.
  show(v.rec[0], wide);
  show(v.rec[1], wide);
  // Down and distance under the clock while live; red in the red zone
  if (wide && live && !down.empty()) {
    put_text(v.down, down.c_str());
    put_color(v.down, f.is_red_zone ? 0xFF3030 : 0xFFFFFF);
    show(v.down, true);
  } else {
    show(v.down, false);
  }
  // Soccer (sport 4): the latest goal on that row, "52' Saka"
  if (wide && f.sport == 4 && live && !situation.empty()) {
    put_text(v.down, situation.c_str());
    put_color(v.down, 0xFFFFFF);
    show(v.down, true);
  }
  // Baseball (sport 1) during a half inning: bases lit gold (red with the
  // bases loaded), the count and outs beside them on a wide board, out dots
  // on a single panel. Timeout pips are football's.
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
  // Basketball (sport 2): the playoff series ("NY lead 2-1") on the
  // situation row of a wide board. down_distance is always empty for it, so
  // the football branch above has hidden the label.
  if (f.sport == 2 && wide && live && !situation.empty()) {
    put_text(v.down, situation.c_str());
    put_color(v.down, 0xFFFFFF);
    show(v.down, true);
  }
  if (wide) {
    // A side with no known count (-1) hides its pips rather than showing them used.
    for (int i = 0; i < 6; i++)
      show(v.pip[i], f.sport == 0 && (i < 3 ? f.team_timeouts : f.opponent_timeouts) >= 0);
    // Soccer: red cards as red marks in the pips' places under the carded
    // side's logo (the next football update recolors them).
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
  // Big game clock while live: its own row on a wide board, the
  // abbreviations' row on a single panel (the logos still say who is who).
  // Before a game only the idle display's My team today sends one, the
  // countdown to the start ("in 2h 15m").
  if (clock_up)
    put_text(v.clock, clock_text.c_str());
  show(v.clock, clock_up);
  show(v.abbr[0], wide || !clock_up);
  show(v.abbr[1], wide || !clock_up);

  // Touchdowns, home runs, goals, walk-offs, buzzer beaters and the win get
  // fireworks on black with the text in the team color; the rest (smaller
  // scores, turnovers, cards, lead changes) keep the solid color flash
  // (espn::splash_fireworks, alerts.h). A "\n" in the text starts a new line
  // ("PHI" over "FUMBLE RECOVERY").
  const std::string splash = panel_text(f.splash_text, true);
  if (!splash.empty()) {
    put_text(v.splash_lbl, splash.c_str());
    const bool big = ::espn::splash_fireworks(splash, (::espn::Sport) f.sport);
    if (big) {
      put_bg(v.splash, 0x000000);
      put_color(v.splash_lbl, lift_color(f.splash_color, 0xE0));
      show(v.fx, true);
    } else {
      show(v.fx, false);
      put_bg(v.splash, f.splash_color);
      put_color(v.splash_lbl, 0xFFFFFF);
    }
    show(v.splash, true);
    r.splash = true;
    r.big_splash = big;
  }
  return r;
}

}  // namespace esphome::gameday

#endif  // USE_LVGL
