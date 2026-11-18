// The board's ticker and a side area's, kept apart
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apps.h"
#include "board_layout.h"
#include "split.h"

namespace espn {

// Dark columns on each side of the divider
static const int kSeamDark = 3;
static const uint32_t kSeamColor = 0x404850;
static const int kTagPx = 7;
static const int kTagGap = 2;
static const int kTagCharPx = 5;  // spleen_8 glyph width in pixels
static const int kTagBytes = kTagPx * kTagPx * 3;
static const uint32_t kTickerPageMs = 3500;
static const int kPagesPerItem = 8;

// Three or four wide: the area beside the board
struct TickerSeam {
  bool on{false};
  int16_t board_w{0};  // the board's ticker, from x 0
  int16_t line_x{0};   // the divider, in the area's x
  int16_t tag_x{0};
};

inline TickerSeam ticker_seam(const SplitArea &a) {
  TickerSeam s;
  s.board_w = a.main_w;
  if (!a.on || a.y != 0 || a.x <= 0)
    return s;
  s.on = true;
  s.board_w = (int16_t) (a.main_w - kSeamDark);
  s.line_x = 0;
  s.tag_x = (int16_t) (1 + kSeamDark);
  return s;
}

enum class TagIcon : uint8_t { NONE = 0, PLANE, CLOUD, CHART, FLAG, BALL };

// An icon, or a team's abbreviation, before each item
struct TickerTag {
  TagIcon icon{TagIcon::NONE};
  char text[5]{};
  uint32_t color{0xFFFFFF};
};

inline int tag_width(const TickerTag &t) {
  if (t.text[0] != '\0')
    return (int) strlen(t.text) * kTagCharPx;
  return t.icon == TagIcon::NONE ? 0 : kTagPx;
}

// The area's words start after the tag
inline int16_t seam_text_x(const TickerSeam &s, const TickerTag &t) {
  const int w = tag_width(t);
  return (int16_t) (s.tag_x + (w > 0 ? w + kTagGap : 0));
}

inline TickerTag area_tag(AreaView v, SplitShow show, const std::string &abbr, uint32_t team_rgb) {
  TickerTag t;
  t.icon = TagIcon::BALL;
  t.color = 0xFFD000;
  if (v == AreaView::NONE || area_clock(v)) {
    t.icon = TagIcon::NONE;
    return t;
  }
  if (v == AreaView::APP) {
    Display d;
    if (area_app(show, d)) {
      switch (d) {
        case Display::FLIGHTS:
          t.icon = TagIcon::PLANE;
          t.color = 0x4FC3F7;
          break;
        case Display::WEATHER:
          t.icon = TagIcon::CLOUD;
          t.color = 0xD8E4F0;
          break;
        case Display::MARKETS:
          t.icon = TagIcon::CHART;
          t.color = 0x3DDC84;
          break;
        case Display::F1:
          t.icon = TagIcon::FLAG;
          t.color = 0xFF3B30;
          break;
        default:
          break;
      }
    }
    return t;
  }
  const bool team = v == AreaView::TEAM || v == AreaView::GAME || v == AreaView::DETAIL;
  if (team && !abbr.empty()) {
    t.icon = TagIcon::NONE;
    strncpy(t.text, abbr.c_str(), sizeof(t.text) - 1);
    t.color = esphome::gameday::lift_color(team_rgb, 0xE0);
  }
  return t;
}

// Halfway to white: softer than the board's
inline uint32_t tag_text_color(uint32_t c) {
  const uint32_t r = (((c >> 16) & 0xFF) + 0xFF) / 2, g = (((c >> 8) & 0xFF) + 0xFF) / 2, b = ((c & 0xFF) + 0xFF) / 2;
  return (r << 16) | (g << 8) | b;
}

// 7x7, bit 6 leftmost
inline const uint8_t *tag_bits(TagIcon i) {
  static const uint8_t kBits[5][kTagPx] = {
      {0x10, 0x18, 0x5C, 0x7F, 0x5C, 0x18, 0x10},  // a plane, nose to the right
      {0x00, 0x18, 0x3C, 0x3E, 0x7F, 0x7F, 0x00},  // a cloud, flat along the bottom
      {0x07, 0x03, 0x05, 0x08, 0x50, 0x20, 0x00},  // a chart rising to the right
      {0x55, 0x6A, 0x55, 0x6A, 0x40, 0x40, 0x40},  // a checkered flag on its pole
      {0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C},  // a ball for scores and games
  };
  return i == TagIcon::NONE ? nullptr : kBits[(int) i - 1];
}

inline bool tag_lit(TagIcon i, int x, int y) {
  const uint8_t *b = tag_bits(i);
  return b != nullptr && x >= 0 && y >= 0 && x < kTagPx && y < kTagPx && ((b[y] >> (kTagPx - 1 - x)) & 1);
}

// RGB565 little endian, then an alpha plane
inline void tag_paint(TagIcon i, uint32_t rgb, uint8_t *buf) {
  const uint16_t c = (uint16_t) ((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) | ((rgb >> 3) & 0x1F));
  const int n = kTagPx * kTagPx;
  for (int k = 0; k < n; k++) {
    const bool on = tag_lit(i, k % kTagPx, k / kTagPx);
    buf[2 * k] = on ? (uint8_t) (c & 0xFF) : 0;
    buf[2 * k + 1] = on ? (uint8_t) (c >> 8) : 0;
    buf[2 * n + k] = on ? 0xFF : 0;
  }
}

// Code points, as LVGL counts glyphs
inline int utf8_count(const std::string &s) {
  int n = 0;
  for (char c : s)
    n += ((unsigned char) c & 0xC0) != 0x80 ? 1 : 0;
  return n;
}

// Bytes of p's first n code points, up to end
inline size_t utf8_bytes(const char *p, size_t end, int n) {
  size_t i = 0;
  for (int k = 0; i < end; i++)
    if (((unsigned char) p[i] & 0xC0) != 0x80 && k++ == n)
      break;
  return i;
}

// Each " | " item in pages that fit, each ending '\n'
__attribute__((noinline)) inline std::string ticker_pages(const std::string &line, int max_chars,
                                                         int per_item = kPagesPerItem) {
  std::string out;
  if (max_chars < 4)
    max_chars = 4;
  const char *s = line.c_str();
  const size_t n = line.size();
  size_t i = 0;
  while (i < n) {
    const char *bar = strstr(s + i, " | ");
    const size_t e = bar != nullptr ? (size_t) (bar - s) : n;
    int pages = 0, cw = 0;
    size_t start = out.size(), prev = start;
    bool cut = false;
    auto close = [&]() {
      out += '\n';
      pages++;
      prev = start;
      start = out.size();
      cw = 0;
    };
    for (size_t k = i; k < e && !cut;) {
      if (s[k] == ' ' || s[k] == '\n') {
        k++;
        continue;
      }
      size_t j = k;
      int ww = 0;
      for (; j < e && s[j] != ' ' && s[j] != '\n'; j++)
        ww += ((unsigned char) s[j] & 0xC0) != 0x80 ? 1 : 0;
      if (cw > 0 && cw + 1 + ww <= max_chars) {
        out += ' ';
        out.append(s + k, j - k);
        cw += 1 + ww;
        k = j;
        continue;
      }
      if (cw > 0)
        close();
      // A word too long for a page is cut
      while (ww > 0) {
        if (pages == per_item) {
          cut = true;
          break;
        }
        if (ww <= max_chars) {
          out.append(s + k, j - k);
          cw = ww;
          break;
        }
        const size_t b = utf8_bytes(s + k, j - k, max_chars);
        out.append(s + k, b);
        k += b;
        ww -= max_chars;
        cw = max_chars;
        close();
      }
      k = j;
    }
    if (cut) {
      // The last page ends "..", marking the rest left out
      out.resize(prev + utf8_bytes(out.c_str() + prev, start - 1 - prev, max_chars - 2));
      out += "..\n";
    } else if (cw > 0) {
      close();
    }
    i = bar != nullptr ? e + 3 : n;
  }
  return out;
}

inline size_t page_count(const std::string &pages) {
  size_t c = 0;
  for (char ch : pages)
    c += ch == '\n' ? 1 : 0;
  return c;
}

inline std::string page_at(const std::string &pages, size_t i) {
  size_t at = 0;
  for (; i > 0 && at < pages.size(); i--)
    at = pages.find('\n', at) + 1;
  const size_t end = pages.find('\n', at);
  return at < pages.size() ? pages.substr(at, end - at) : std::string();
}

// Paged: still pages; else the usual scrolling line
inline bool area_ticker_paged(const TickerSeam &s, bool clock) { return s.on && !clock; }

inline size_t ticker_page_next(size_t page, size_t pages) { return pages == 0 ? 0 : (page + 1) % pages; }

}  // namespace espn
