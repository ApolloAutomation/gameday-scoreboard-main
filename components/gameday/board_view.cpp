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
#include "logo_lru.h"
#include "panel_text.h"
#include "scene_view.h"
#include "setup_qr.h"

static_assert(LV_TXT_COLOR_CMD[0] == esphome::gameday::kRecolorCmd, "__init__.py defines LV_TXT_COLOR_CMD");

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

// Off unless LVGL's own code reads our marker
bool ticker_colors = false;

bool recolor_works(lv_obj_t *label) {
  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  lv_point_t marked, plain;
  lv_text_get_size(&marked, "`FFFFFF A`", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_RECOLOR);
  lv_text_get_size(&plain, "A", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return marked.x == plain.x;
}

}  // namespace

bool ticker_colors_on() { return ticker_colors; }

namespace {
std::string lead_text;
uint32_t lead_rgb = 0;
}  // namespace

void board_ticker_lead(const std::string &text, uint32_t rgb) {
  lead_text = text;
  lead_rgb = rgb;
}

namespace {

std::string ticker_line(const UpdateFields &f) {
  const std::string line = panel_text(f.status_text);
  return ticker_colors ? ticker_markup(line, f.ticker_spans) : line;
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

// Large game clock where its text fits
void put_clock(BoardView &v, const GameGeom &g, bool force) {
  if (v.clock == nullptr)
    return;
  const char *t = lv_label_get_text(v.clock);
  const ClockPick p = game_clock(g, t != nullptr ? t : "", v.clock_rec, v.clock_baseball);
  const lv_font_t *f = font_of(v, p.font);
  if (!force && lv_obj_get_style_text_font(v.clock, LV_PART_MAIN) == f)
    return;
  put_font(v.clock, f);
  place(v.clock, p.spot);
}

// Black behind fireworks, else a flash of the color
void splash_paint(BoardView &v, bool fx, uint32_t color) {
  const uint32_t ink = fx ? lift_color(color, 0xE0) : 0xFFFFFF;
  put_bg(v.splash, fx ? 0x000000 : color);
  put_color(v.splash_lbl, ink);
  put_color(v.splash_pts, ink);
  // Black box: bursts never cross the points
  if (v.splash_pts != nullptr) {
    lv_obj_set_style_bg_color(v.splash_pts, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(v.splash_pts, fx && v.pts_mask ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

// One canvas: the board's splash or the second area's
enum class FxHost : uint8_t { NONE, BOARD, AREA };
FxHost fx_host = FxHost::NONE;

bool fx_start(lv_obj_t *splash, FxHost who, const Spot &at, int h) {
  BoardView *m = page_view;
  if (m == nullptr || m->fx == nullptr || m->fx_run == nullptr || splash == nullptr)
    return false;
  // The board takes it back; the area flashes instead
  if (fx_host == FxHost::AREA && who == FxHost::BOARD && area_ptr != nullptr)
    splash_paint(area_ptr->game, false, area_ptr->splash_color);
  if (lv_obj_get_parent(m->fx) != splash)
    lv_obj_set_parent(m->fx, splash);
  lv_obj_move_to_index(m->fx, 0);
  lv_obj_set_pos(m->fx, at.x, at.y);
  lv_obj_set_size(m->fx, std::min<int>(at.w, kFxPx), std::min(h, kBoardRows));
  show(m->fx, true);
  m->fx_run(true);
  fx_host = who;
  return true;
}

void fx_stop(FxHost who) {
  BoardView *m = page_view;
  if (fx_host != who || m == nullptr)
    return;
  if (m->fx_run != nullptr)
    m->fx_run(false);
  show(m->fx, false);
  fx_host = FxHost::NONE;
}

void line_style(lv_obj_t *line) {
#if LV_USE_LINE
  if (line == nullptr)
    return;
  lv_obj_set_pos(line, 0, 0);
  lv_obj_set_style_line_width(line, 1, 0);
  lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
  lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
#endif
}

// Shown on a leaderboard with two points or more
void put_line(BoardView &v, const UpdateFields &f) {
#if LV_USE_LINE
  if (v.line == nullptr)
    return;
  const size_t n = std::min(f.line.size() / 2, ::espn::kAppLinePoints);
  if (!f.board || n < 2) {
    show(v.line, false);
    return;
  }
  for (size_t i = 0; i < n; i++) {
    v.line_pts[i].x = f.line[2 * i];
    v.line_pts[i].y = f.line[2 * i + 1];
  }
  lv_obj_set_style_line_color(v.line, lv_color_hex(f.line_color), 0);
  lv_line_set_points_mutable(v.line, v.line_pts, (uint32_t) n);
  show(v.line, true);
#endif
}

// Each label in order; hidden past the frame's
void put_texts(BoardView &v, const UpdateFields &f) {
  const size_t n = f.board ? std::min(f.texts.size(), ::espn::kAppTexts) : 0;
  for (size_t i = 0; i < ::espn::kAppTexts; i++) {
    lv_obj_t *&l = v.text[i];
    if (i >= n) {
      if (l == nullptr)
        break;
      show(l, false);
      continue;
    }
    if (l == nullptr) {
      if (v.board == nullptr || v.f8 == nullptr)
        return;
      l = plain_label(v.board, v.f8, LV_OPA_COVER);
    }
    const ::espn::AppText &t = f.texts[i];
    const lv_font_t *font = font_of(v, t.font);
    if (lv_obj_get_style_text_font(l, LV_PART_MAIN) != font)
      lv_obj_set_style_text_font(l, font, 0);
    if (lv_obj_get_style_x(l, LV_PART_MAIN) != t.x || lv_obj_get_style_y(l, LV_PART_MAIN) != t.y)
      lv_obj_set_pos(l, t.x, t.y);
    if (lv_obj_get_style_width(l, LV_PART_MAIN) != t.w)
      lv_obj_set_width(l, t.w);
    const lv_text_align_t align = t.align == ::espn::AppAlign::RIGHT    ? LV_TEXT_ALIGN_RIGHT
                                  : t.align == ::espn::AppAlign::CENTER ? LV_TEXT_ALIGN_CENTER
                                                                        : LV_TEXT_ALIGN_LEFT;
    if (lv_obj_get_style_text_align(l, LV_PART_MAIN) != align)
      lv_obj_set_style_text_align(l, align, 0);
    put_color(l, t.color);
    put_changed(l, t.text.c_str());
    show(l, true);
  }
}

// Each logo in order; hidden past the frame's
void put_icons(BoardView &v, const UpdateFields &f) {
  const size_t n = f.board ? std::min((size_t) f.icon_n, ::espn::kAppIcons) : 0;
  for (size_t i = 0; i < ::espn::kAppIcons; i++) {
    lv_obj_t *&o = v.icon[i];
    if (i >= n) {
      if (o == nullptr)
        break;
      show(o, false);
      continue;
    }
    if (o == nullptr) {
      if (v.board == nullptr)
        return;
      o = lv_image_create(v.board);
      lv_obj_remove_style_all(o);
    }
    const lv_image_dsc_t *src = f.icon_img[i]->get_lv_image_dsc();
    if (lv_image_get_src(o) != src)
      ::lv_image_set_src(o, src);
    const int16_t x = f.icon_xy[2 * i], y = f.icon_xy[2 * i + 1];
    if (lv_obj_get_style_x(o, LV_PART_MAIN) != x || lv_obj_get_style_y(o, LV_PART_MAIN) != y)
      lv_obj_set_pos(o, x, y);
    show(o, true);
  }
}

// The box under the board's labels, made on first use
void put_bar(BoardView &v, const UpdateFields &f) {
  const ::espn::AppFill &b = f.bar;
  const bool on = f.board && b.w > 0 && b.h > 0;
  if (v.bar == nullptr) {
    if (!on || v.board == nullptr)
      return;
    v.bar = plain_bar(v.board, b.color);
    lv_obj_move_to_index(v.bar, 0);
    if (v.photo != nullptr)
      lv_obj_move_to_index(v.photo, 0);
  }
  if (on) {
    if (lv_obj_get_style_x(v.bar, LV_PART_MAIN) != b.x || lv_obj_get_style_y(v.bar, LV_PART_MAIN) != b.y)
      lv_obj_set_pos(v.bar, b.x, b.y);
    if (lv_obj_get_style_width(v.bar, LV_PART_MAIN) != b.w || lv_obj_get_style_height(v.bar, LV_PART_MAIN) != b.h)
      lv_obj_set_size(v.bar, b.w, b.h);
    put_bg(v.bar, b.color);
  }
  show(v.bar, on);
}

// Team logo screen's logos, or its abbreviation
void logo_show(BoardView &v, image::Image *img) {
  if (v.logo_abbr == nullptr)
    return;
  const LogoScreenGeom g = logo_screen_geom(v.w);
  const int off = logo_obj_offset(g.logo_px);
  for (int i = 0; i < 2; i++) {
    lv_obj_t *o = v.logo_img[i];
    const bool on = img != nullptr && i < g.logos;
    if (on) {
      ::lv_image_set_src(o, img->get_lv_image_dsc());
      lv_image_set_scale(o, (uint32_t) logo_scale(g.logo_px));
      put_pos(o, g.logo_x[i] + off, g.logo_y + off);
    }
    show(o, on);
  }
  put_pos(v.logo_abbr, g.logo_x[0], g.logo_y + (g.logo_px - font_rows(BoardFont::S16)) / 2);
  put_width(v.logo_abbr, g.logo_px);
  show(v.logo_abbr, img == nullptr);
}

void logo_hide(BoardView &v) {
  for (lv_obj_t *o : v.logo_img)
    show(o, false);
  show(v.logo_abbr, false);
  show(v.logo_name, false);
  for (lv_obj_t *o : v.logo_line)
    show(o, false);
}

// Made on first use: most panels never show it
void logo_build(BoardView &v) {
  for (lv_obj_t *&img : v.logo_img) {
    img = lv_image_create(v.no_game);
    lv_obj_remove_style_all(img);
    // Doubled pixels stay crisp
    lv_image_set_antialias(img, false);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  }
  v.logo_abbr = plain_label(v.no_game, v.f16, LV_OPA_COVER);
  v.logo_name = plain_label(v.no_game, v.f12, LV_OPA_COVER);
  for (lv_obj_t *&l : v.logo_line)
    l = plain_label(v.no_game, v.f8, LV_OPA_COVER);
}

void logo_draw(BoardView &v, const UpdateFields &f, image::Image *img) {
  if (f.logo_screen && v.logo_name == nullptr && v.no_game != nullptr && &v == page_view)
    logo_build(v);
  if (!f.logo_screen || v.logo_name == nullptr) {
    logo_hide(v);
    return;
  }
  show(v.msg, false);
  const LogoScreenGeom g = logo_screen_geom(v.w);
  const std::string abbr = panel_text(f.team_abbr);
  put_font(v.logo_name, font_of(v, g.name_font));
  place(v.logo_name, g.name);
  put_changed(v.logo_name, ::espn::logo_name(panel_text(f.message), abbr, spot_chars(g.name, g.name_font)).c_str());
  put_color(v.logo_name, f.team_color);
  show(v.logo_name, true);
  put_changed(v.logo_abbr, abbr.c_str());
  put_color(v.logo_abbr, f.team_color);
  // Opponent, when, then a shorter when
  std::string part[3];
  size_t at = 0;
  for (int i = 0; i < 3 && at <= f.next_line.size(); i++) {
    const size_t nl = f.next_line.find('\n', at);
    part[i] = panel_text(f.next_line.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
    at = nl == std::string::npos ? f.next_line.size() + 1 : nl + 1;
  }
  const size_t fit = spot_chars(g.line[0], g.line_font);
  const std::string text[2] = {part[0], part[1].size() <= fit ? part[1] : part[2]};
  // The address ticker takes 64 wide's bottom line
  const bool ticker_row = f.join_line && v.w < 128;
  for (int i = 0; i < 2; i++) {
    put_font(v.logo_line[i], font_of(v, g.line_font));
    place(v.logo_line[i], g.line[i]);
    put_changed(v.logo_line[i], text[i].c_str());
    show(v.logo_line[i], !text[i].empty() && !(i == 1 && ticker_row));
  }
  logo_show(v, img);
}

uint32_t art_key(const ::espn::AppArt &a) {
  const uint32_t v[] = {(uint32_t) (uintptr_t) a.bits, (uint32_t) (a.w | a.h << 8 | a.flip << 16 | a.scale << 17),
                        a.ink[0], a.ink[1], a.ink[2]};
  return ::espn::logo_key(reinterpret_cast<const char *>(v), sizeof(v)) | 1;
}

// Repainted in place: LVGL must not cache it.
static_assert(LV_CACHE_DEF_SIZE == 0, "put_art repaints one buffer; drop it from the image cache");

// The frame's pictures; each repainted only when it changes
void put_art(BoardView &v, const UpdateFields &f) {
  const size_t n = f.board ? std::min(f.art.size(), ::espn::kAppArt) : 0;
  for (size_t i = 0; i < ::espn::kAppArt; i++) {
    BoardView::Art &slot = v.art[i];
    if (i >= n) {
      if (slot.obj == nullptr)
        break;
      show(slot.obj, false);
      continue;
    }
    const ::espn::AppArt &a = f.art[i];
    const uint32_t w = (uint32_t) a.w * a.scale, h = (uint32_t) a.h * a.scale, bytes = w * h * 3;
    // Grown to fit; never shrunk
    if (bytes > slot.cap) {
      uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (buf == nullptr)
        return;
      if (slot.obj != nullptr)
        ::lv_image_set_src(slot.obj, nullptr);
      heap_caps_free(slot.buf);
      slot.buf = buf;
      slot.cap = bytes;
      slot.key = 0;
    }
    if (slot.obj == nullptr) {
      if (v.board == nullptr)
        return;
      slot.obj = lv_image_create(v.board);
      lv_obj_remove_style_all(slot.obj);
    }
    const uint32_t key = art_key(a);
    if (key != slot.key) {
      ::espn::app_art_paint(a, slot.buf);
      lv_image_dsc_t &d = slot.dsc;
      d.header.magic = LV_IMAGE_HEADER_MAGIC;
      d.header.cf = LV_COLOR_FORMAT_RGB565A8;
      d.header.w = w;
      d.header.h = h;
      d.header.stride = w * 2;
      d.data_size = bytes;
      d.data = slot.buf;
      ::lv_image_set_src(slot.obj, &d);
      lv_obj_invalidate(slot.obj);
      slot.key = key;
    }
    if (lv_obj_get_style_x(slot.obj, LV_PART_MAIN) != a.x || lv_obj_get_style_y(slot.obj, LV_PART_MAIN) != a.y)
      lv_obj_set_pos(slot.obj, a.x, a.y);
    show(slot.obj, true);
  }
}

// Only the pixels that changed redraw
void put_field(BoardView &v, const UpdateFields &f) {
  const ::espn::FieldPic &pic = f.field_pic;
  if (!f.board || !f.field || pic.w <= 0 || pic.h <= 0) {
    show(v.field, false);
    return;
  }
  const size_t n = (size_t) pic.w * pic.h;
  if (n > v.field_px) {
    heap_caps_free(v.field_buf);
    heap_caps_free(v.field_next);
    v.field_buf = static_cast<uint16_t *>(heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    v.field_next = static_cast<uint16_t *>(heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    v.field_px = v.field_buf != nullptr && v.field_next != nullptr ? n : 0;
    v.field_shown = ::espn::FieldPic();
    if (v.field_px == 0) {
      show(v.field, false);
      return;
    }
  }
  if (v.field == nullptr) {
    if (v.board == nullptr)
      return;
    v.field = lv_canvas_create(v.board);
    lv_obj_remove_flag(v.field, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_move_to_index(v.field, 0);
  }
  const ::espn::FieldPic &was = v.field_shown;
  const bool fresh = was.w != pic.w || was.h != pic.h || was.x != pic.x || was.y != pic.y;
  if (fresh) {
    lv_canvas_set_buffer(v.field, v.field_buf, pic.w, pic.h, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(v.field, pic.x, pic.y);
    lv_obj_set_size(v.field, pic.w, pic.h);
  }
  if (fresh || !::espn::field_same(pic, v.field_shown)) {
    ::espn::Px565 p;
    p.px = v.field_next;
    p.w = p.stride = pic.w;
    p.h = pic.h;
    p.clip = ::espn::PxBox{0, 0, pic.w, pic.h};
    ::espn::field_paint(p, pic);
    int x0 = pic.w, y0 = pic.h, x1 = -1, y1 = -1;
    for (int y = 0; y < pic.h; y++)
      for (int x = 0; x < pic.w; x++) {
        const size_t i = (size_t) y * pic.w + x;
        if (!fresh && v.field_buf[i] == v.field_next[i])
          continue;
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
        v.field_buf[i] = v.field_next[i];
      }
    if (fresh) {
      lv_obj_invalidate(v.field);
    } else if (x1 >= 0) {
      // LVGL takes screen coordinates: the area's are offset
      lv_area_t c, a;
      lv_obj_get_coords(v.field, &c);
      lv_area_set(&a, c.x1 + x0, c.y1 + y0, c.x1 + x1, c.y1 + y1);
      lv_obj_invalidate_area(v.field, &a);
    }
    v.field_shown = pic;
  }
  show(v.field, true);
}

// The frame's photo, from the logo cache
void put_photo(BoardView &v, const UpdateFields &f) {
  if (!f.board || f.photo_img == nullptr) {
    show(v.photo, false);
    return;
  }
  if (v.photo == nullptr) {
    if (v.board == nullptr)
      return;
    v.photo = lv_image_create(v.board);
    lv_obj_remove_style_all(v.photo);
    // Under the labels, lines and pictures
    lv_obj_move_to_index(v.photo, 0);
  }
  const lv_image_dsc_t *src = f.photo_img->get_lv_image_dsc();
  if (lv_image_get_src(v.photo) != src)
    ::lv_image_set_src(v.photo, src);
  const int16_t x = f.photo_xy[0], y = f.photo_xy[1];
  if (lv_obj_get_style_x(v.photo, LV_PART_MAIN) != x || lv_obj_get_style_y(v.photo, LV_PART_MAIN) != y)
    lv_obj_set_pos(v.photo, x, y);
  show(v.photo, true);
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

void board_build(BoardView &v) {
  if (v.ticker != nullptr) {
    ticker_colors = recolor_works(v.ticker);
    lv_label_set_recolor(v.ticker, ticker_colors);
  }
  if (v.clock_ampm == nullptr && v.no_game != nullptr && v.clock_time != nullptr) {
    v.clock_ampm = plain_label(v.no_game, v.f8, LV_OPA_COVER);
    lv_obj_add_flag(v.clock_ampm, LV_OBJ_FLAG_HIDDEN);
  }
  // Fireworks fill the canvas's top left
  if (v.fx != nullptr)
    lv_image_set_inner_align(v.fx, LV_IMAGE_ALIGN_TOP_LEFT);
  line_style(v.line);
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
  v.h = (int16_t) (h > kBoardRows ? h : kBoardRows);
  v.splash_h = (int16_t) splash_h;
  v.sizes = sizes;
  const GameGeom g = game_geom(w, sizes.text, sizes.clock);
  const SplashBox sb = splash_box(w, splash_h);
  show(v.ticker, true);
  put_size(v.game, w, kBoardRows);
  put_size(v.no_game, w, v.h);
  put_size(v.board, w, v.h);
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
  put_clock(v, g, true);
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

namespace {

// The scene's splash: its fireworks wait for the scene's end
uint32_t scene_seq = 0;
uint32_t stay_ms = ::espn::kSplashMs;
bool fx_wait = false;
Spot fx_wait_at{};
int16_t fx_wait_h = 0;
uint32_t fx_wait_color = 0;

void scene_reveal() {
  BoardView *m = page_view;
  if (m == nullptr || !fx_wait)
    return;
  fx_wait = false;
  if (m->splash == nullptr || lv_obj_has_flag(m->splash, LV_OBJ_FLAG_HIDDEN))
    return;
  if (!fx_start(m->splash, FxHost::BOARD, fx_wait_at, fx_wait_h))
    splash_paint(*m, false, fx_wait_color);
}

// A redraw of the same splash never replays it
bool splash_scene(BoardView &v, const UpdateFields &f) {
  if (f.splash_seq != 0 && f.splash_seq == scene_seq)
    return scene_playing();
  scene_seq = f.splash_seq;
  scene_stop();
  fx_wait = false;
  bool on = false;
  if (f.splash_anim != 0 && f.splash_seq != 0) {
    const bool opp = f.splash_side == 1;
    const ::espn::AnimArgs a{(::espn::AnimKind) f.splash_anim, f.splash_anim_dist, f.splash_anim_flags};
    on = scene_play(v.splash, a, opp ? f.opponent_color : f.team_color, opp ? f.team_color : f.opponent_color,
                    f.splash_seq, scene_reveal, f.splash_say.c_str(), f.splash_score.c_str());
  }
  stay_ms = ::espn::kSplashMs + (on ? ::espn::scene_ms((::espn::AnimKind) f.splash_anim) : 0);
  return on;
}

}  // namespace

DrawResult board_draw(BoardView &v, const UpdateFields &f, image::Image *team_logo, image::Image *opp_logo) {
  DrawResult r;
  const int W = v.w;
  const bool wide = W >= 128;
  const GameGeom g = game_geom(W, v.sizes.text, v.sizes.clock);
  const bool live = f.game_state == "IN";
  // panel_text() so missing glyphs never draw as boxes
  const std::string abbr[2] = {panel_text(f.team_abbr), panel_text(f.opponent_abbr)};
  std::string status = ticker_line(f);
  // The main board's warning, unless its ticker says it already
  if (&v == page_view && !lead_text.empty() && f.status_text.find(lead_text) == std::string::npos) {
    const std::string lead = panel_text(lead_text);
    status = (ticker_colors ? ticker_markup(lead, {{lead, lead_rgb}}) : lead) + (status.empty() ? "" : " | ") + status;
  }
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

  // Before a game, only the idle countdown sends a clock
  const bool clock_up = (live || f.game_state == "PRE") && !clock_text.empty();
  if (clock_up)
    put_text(v.clock, clock_text.c_str());
  v.clock_rec = (uint8_t) std::min<size_t>(wide ? std::max(rec[0].size(), rec[1].size()) : 0, 255);
  v.clock_baseball = f.sport == 1;
  put_clock(v, g, false);
  // College rank goes in front only where it fits
  const int clock_px = clock_up && v.clock != nullptr ? (int) clock_text.size() * char_px(v.clock) : 0;
  auto put_rank = [&](lv_obj_t *lbl, uint8_t rank, const std::string &text, bool row) {
    if (lbl == nullptr)
      return;
    const int cw = char_px(lbl);
    const int lw = (int) lv_obj_get_style_width(lbl, LV_PART_MAIN);
    const size_t fit = cw <= 0 || lw <= 0 ? 0 : row ? record_fit(g, clock_px, cw) : (size_t) (lw / cw);
    lv_label_set_text(lbl, ::espn::rank_label(rank, text, fit).c_str());
  };
  const bool abbr_rank = !wide && !live;
  put_rank(v.abbr[0], abbr_rank ? f.team_rank : 0, abbr[0], false);
  put_rank(v.abbr[1], abbr_rank ? f.opponent_rank : 0, abbr[1], false);
  put_rank(v.rec[0], wide ? f.team_rank : 0, rec[0], true);
  put_rank(v.rec[1], wide ? f.opponent_rank : 0, rec[1], true);
  // Large clock: records slide out to clear it
  for (int i = 0; i < 2 && wide; i++) {
    if (v.rec[i] == nullptr)
      continue;
    const int px = (int) strlen(lv_label_get_text(v.rec[i])) * char_px(v.rec[i]);
    const int x = record_label_x(g, i, px, clock_px);
    if (lv_obj_get_style_x(v.rec[i], LV_PART_MAIN) != x)
      lv_obj_set_x(v.rec[i], x);
  }

  // Rewriting restarts the scroll; leaderboards fit only spleen_8
  if (v.ticker != nullptr) {
    // The Field view keeps the Text size
    const bool rows = f.board && !f.field;
    const lv_font_t *tf = rows ? v.f8 : font_of(v, g.ticker_font);
    if (tf != nullptr && lv_obj_get_style_text_font(v.ticker, LV_PART_MAIN) != tf) {
      lv_obj_set_style_text_font(v.ticker, tf, 0);
      r.ticker_changed = true;
    }
    const int ty = rows ? v.h - 8 : g.ticker_y + v.h - kBoardRows;
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
  put_bar(v, f);
  put_line(v, f);
  put_texts(v, f);
  put_icons(v, f);
  put_art(v, f);
  put_photo(v, f);
  put_field(v, f);
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
  logo_draw(v, f, team_logo);
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
    v.pts_mask = L.pts_mask;
    r.big_splash = ::espn::splash_fireworks(splash, (::espn::Sport) f.sport);
    // With a logo, clear of the words; else the band
    const SplashBox sb = splash_box(W, v.splash_h);
    r.fx = L.logos ? L.fx : Spot{(int16_t) sb.fx_x, (int16_t) sb.band_y, (int16_t) std::min(W, kFxPx)};
    r.fx_h = L.logos ? L.fx_h : (int16_t) kBoardRows;
    bool fx = false, wait = false;
    if (&v == page_view) {
      const bool scene = splash_scene(v, f);
      wait = scene && r.big_splash;
      if (wait) {
        fx_wait = true;
        fx_wait_at = r.fx;
        fx_wait_h = r.fx_h;
        fx_wait_color = f.splash_color;
      }
      fx = !scene && r.big_splash && fx_start(v.splash, FxHost::BOARD, r.fx, r.fx_h);
      if (!fx)
        fx_stop(FxHost::BOARD);
    }
    splash_paint(v, fx || wait, f.splash_color);
    show(v.splash, true);
    r.splash = true;
  } else if (logos_key(f) != v.tile_logos) {
    // Board moved on; a late logo is another game's
    v.tile_waiting[0] = v.tile_waiting[1] = false;
  }
  return r;
}

uint32_t splash_stay_ms() { return stay_ms; }

void board_hide_splash(BoardView &v) {
  if (&v == page_view) {
    scene_stop();
    fx_wait = false;
  }
  show(v.splash, false);
  fx_stop(FxHost::BOARD);
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
  if (v.logo_name != nullptr && !lv_obj_has_flag(v.logo_name, LV_OBJ_FLAG_HIDDEN))
    logo_show(v, team);
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
  if (ticker != nullptr)
    lv_label_set_recolor(ticker, ticker_colors);
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
  // A leaderboard or another display's rows, and its line
  v.board = plain_obj(root);
  lv_obj_add_flag(v.board, LV_OBJ_FLAG_HIDDEN);
  RowsView &r = a->rows;
  r.title = plain_label(v.board, f8, 230);
  lv_obj_set_style_text_align(r.title, LV_TEXT_ALIGN_LEFT, 0);
  r.status = plain_label(v.board, f8, LV_OPA_COVER);
  for (int i = 0; i < kRowsCount; i++) {
    r.pos[i] = plain_label(v.board, f8, 191);
    r.name[i] = plain_label(v.board, f8, 230);
    r.value[i] = plain_label(v.board, f8, LV_OPA_COVER);
    r.detail[i] = plain_label(v.board, f8, 191);
    lv_obj_set_style_text_align(r.name[i], LV_TEXT_ALIGN_LEFT, 0);
    for (lv_obj_t *c : {r.pos[i], r.value[i], r.detail[i]})
      lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_RIGHT, 0);
    put_color(r.pos[i], 0xA0A0A0);
    put_color(r.detail[i], 0xA0A0A0);
  }
#if LV_USE_LINE
  v.line = lv_line_create(v.board);
  lv_obj_remove_style_all(v.line);
#endif
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
  const RowsGeom g = rows_geom(w);
  RowsView &r = a.rows;
  r.wide = g.wide;
  place(r.title, g.title);
  show(r.title, g.wide);
  place(r.status, g.status);
  if (r.status != nullptr)
    lv_obj_set_style_text_align(r.status, g.wide ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < kRowsCount; i++) {
    const int16_t y = (int16_t) rows_y(i);
    place(r.pos[i], Spot{g.pos.x, y, g.pos.w});
    place(r.name[i], Spot{g.name.x, y, g.name.w});
    place(r.value[i], Spot{g.value.x, y, g.value.w});
    place(r.detail[i], Spot{g.detail.x, y, g.detail.w});
    show(r.detail[i], g.wide);
  }
}

namespace {

// Set only on change: each set restarts the scroll
void put_y(lv_obj_t *o, int y) {
  if (o != nullptr && lv_obj_get_style_y(o, LV_PART_MAIN) != y)
    lv_obj_set_y(o, y);
}

// gd_board's drawing, for the area's labels
void rows_draw(RowsView &r, const UpdateFields &f, int w) {
  put_changed(r.title, f.board_title.c_str());
  put_changed(r.status, f.board_status.c_str());
  put_color(r.status, f.board_status_color);
  rows_put(r.pos, r.name, r.value, r.detail, w, f);
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

void rows_put(lv_obj_t *const *pos, lv_obj_t *const *name, lv_obj_t *const *value, lv_obj_t *const *detail, int w,
              const UpdateFields &f) {
  auto at = [](const std::vector<std::string> &v, int i) { return i < (int) v.size() ? v[i].c_str() : ""; };
  auto tint = [](const std::vector<int> &v, int i) { return i < (int) v.size() ? (uint32_t) v[i] : 0xA0A0A0u; };
  const RowsCells c = rows_cells(w, f.board_value_narrow, f.board_cols);
  // Leaderboards dim place and detail; displays don't
  static const lv_opa_t kOpa[4] = {191, 230, LV_OPA_COVER, 191};
  for (int i = 0; i < kRowsCount; i++) {
    lv_obj_t *const cell[4] = {pos[i], name[i], value[i], detail[i]};
    for (int k = 0; k < 4; k++) {
      lv_obj_t *o = cell[k];
      if (o == nullptr)
        continue;
      const Spot &at_k = c.cell[k];
      show(o, at_k.w > 0);
      if (at_k.w > 0 && lv_obj_get_style_x(o, LV_PART_MAIN) != at_k.x)
        lv_obj_set_x(o, at_k.x);
      if (at_k.w > 0 && lv_obj_get_style_width(o, LV_PART_MAIN) != at_k.w)
        lv_obj_set_width(o, at_k.w);
      const lv_opa_t opa = c.own ? (lv_opa_t) LV_OPA_COVER : kOpa[k];
      if (lv_obj_get_style_text_opa(o, LV_PART_MAIN) != opa)
        lv_obj_set_style_text_opa(o, opa, 0);
    }
    put_changed(pos[i], at(f.board_pos, i));
    put_changed(name[i], at(f.board_name, i));
    put_changed(value[i], at(f.board_value, i));
    put_changed(detail[i], at(f.board_detail, i));
    put_color(pos[i], tint(f.board_pos_color, i));
    put_color(detail[i], tint(f.board_detail_color, i));
    if (i < (int) f.board_value_color.size())
      put_color(value[i], (uint32_t) f.board_value_color[i]);
    if (i < (int) f.board_name_color.size())
      put_color(name[i], (uint32_t) f.board_name_color[i]);
  }
}

DrawResult area_draw(AreaView &a, const UpdateFields &f, bool clock, bool main_clock, bool main_date,
                     image::Image *team_logo, image::Image *opp_logo) {
  BoardView &v = a.game;
  const std::string line = ticker_line(f);
  DrawResult r;
  if (clock) {
    show(v.game, false);
    show(v.no_game, false);
    show(v.board, false);
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
  const GameGeom g = game_geom(a.w, v.sizes.text, v.sizes.clock);
  // Leaderboards fit only spleen_8, on the last row
  if (f.board)
    rows_draw(a.rows, f, a.w);
  r.ticker_changed = f.board && !f.field ? put_ticker(a.ticker, v.f8, kBoardRows - 8, line)
                                         : put_ticker(a.ticker, font_of(v, g.ticker_font), g.ticker_y, line);
  if (r.splash) {
    // Fireworks when the board isn't using them
    a.splash_color = f.splash_color;
    const bool fx = r.big_splash && fx_host != FxHost::BOARD && fx_start(v.splash, FxHost::AREA, r.fx, r.fx_h);
    if (!fx)
      fx_stop(FxHost::AREA);
    splash_paint(v, fx, f.splash_color);
  }
  return r;
}

void area_hide_splash(AreaView &a) {
  show(a.game.splash, false);
  fx_stop(FxHost::AREA);
}

void area_clear(AreaView &a) {
  BoardView &v = a.game;
  show(v.game, false);
  show(v.no_game, false);
  show(v.board, false);
  show(v.splash, false);
  fx_stop(FxHost::AREA);
  show(a.time, false);
  show(a.ampm, false);
  show(a.date, false);
  if (a.ticker != nullptr)
    lv_label_set_text(a.ticker, "");
}

}  // namespace esphome::gameday

#endif  // USE_LVGL
