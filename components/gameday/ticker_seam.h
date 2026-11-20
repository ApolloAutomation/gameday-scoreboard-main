// The board's ticker and a side area's, kept apart
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "apps.h"
#include "board_layout.h"
#include "panel_text.h"
#include "split.h"

namespace espn {

using esphome::gameday::kRecolorCmd;

// The board's ticker stops this far short
static const int kSeamDark = 5;
// Dark columns between the divider and the tag
static const int kSeamPad = 3;
static const int kSeamLinePx = 2;
static const uint32_t kSeamColor = 0x607088;
static const int kTagPx = 7;
static const int kTagGap = 3;
static const int kTagCharPx = 5;  // spleen_8 glyph width in pixels
static const int kTagBytes = kTagPx * kTagPx * 3;
static const uint32_t kTickerPageMs = 3500;
static const int kPagesPerItem = 8;
// Starts a page that carries on an item
static const char kPageMore = '\t';

// Three or four wide: the area beside the board
struct TickerSeam {
  bool on{false};
  int16_t board_w{0};  // the board's ticker, from x 0
  int16_t line_x{0};   // the divider, in the area's x
  int16_t tag_x{0};
  int16_t main_w{0};  // the board's full width
};

inline TickerSeam ticker_seam(const SplitArea &a) {
  TickerSeam s;
  s.board_w = a.main_w;
  s.main_w = a.main_w;
  if (!a.on || a.y != 0 || a.x <= 0)
    return s;
  s.on = true;
  s.board_w = (int16_t) (a.main_w - kSeamDark);
  s.line_x = 0;
  s.tag_x = (int16_t) (kSeamLinePx + kSeamPad);
  return s;
}

enum class TagIcon : uint8_t { NONE = 0, PLANE, CLOUD, CHART, FLAG, BALL, MORE };

// An icon, or a team's abbreviation, before each item
struct TickerTag {
  TagIcon icon{TagIcon::NONE};
  char text[9]{};  // up to four letters, UTF-8
  uint32_t color{0xFFFFFF};
};

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

inline int tag_width(const TickerTag &t) {
  if (t.text[0] != '\0')
    return utf8_count(t.text) * kTagCharPx;
  return t.icon == TagIcon::NONE ? 0 : kTagPx;
}

// The area's words start after the tag
inline int16_t seam_text_x(const TickerSeam &s, const TickerTag &t) {
  const int w = tag_width(t);
  // A word tag gets a full space
  const int gap = t.text[0] != '\0' ? kTagCharPx : kTagGap;
  return (int16_t) (s.tag_x + (w > 0 ? w + gap : 0));
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
          t.color = 0x78A8F0;
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
  const std::string a = team ? esphome::gameday::panel_text(abbr) : std::string();
  if (!a.empty()) {
    t.icon = TagIcon::NONE;
    memcpy(t.text, a.data(), utf8_bytes(a.data(), std::min(a.size(), sizeof(t.text) - 1), 4));
    t.color = esphome::gameday::lift_color(team_rgb, 0xFF);
  }
  return t;
}

// Toward gray, so never the board's white
inline uint32_t tag_text_color(uint32_t c) {
  const uint32_t r = (((c >> 16) & 0xFF) + 0xB0) / 2, g = (((c >> 8) & 0xFF) + 0xB0) / 2, b = ((c & 0xFF) + 0xB0) / 2;
  return (r << 16) | (g << 8) | b;
}

// 7x7, bit 6 leftmost
inline const uint8_t *tag_bits(TagIcon i) {
  static const uint8_t kBits[6][kTagPx] = {
      {0x08, 0x1C, 0x1C, 0x7F, 0x08, 0x1C, 0x00},  // a plane, nose up
      {0x00, 0x06, 0x37, 0x7F, 0x7F, 0x3E, 0x00},  // a cloud, two bumps
      {0x07, 0x03, 0x05, 0x08, 0x50, 0x20, 0x00},  // a chart rising to the right
      {0x55, 0x6A, 0x55, 0x6A, 0x40, 0x40, 0x40},  // a checkered flag on its pole
      {0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C},  // a ball for scores and games
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x00},  // dots: the item carries on
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

// One word of an item, its color kept
struct PageWord {
  uint32_t at, len;  // bytes into the line
  uint32_t rgb;      // 0: the label's own color
  int chars;
};

inline bool word_numeric(const char *w, size_t n) {
  const size_t i = n > 1 && (w[0] == '+' || w[0] == '-' || w[0] == '$') ? 1 : 0;
  return i < n && w[i] >= '0' && w[i] <= '9';
}

// Higher is a better place to break
inline int break_score(const char *s, const PageWord &a, const PageWord &b) {
  const char last = s[a.at + a.len - 1], next = s[b.at];
  if (last == ',' || last == ':' || last == ';')
    return 3;
  const bool num = (last >= '0' && last <= '9') || last == '%';
  // Keep "DAL 27", "8 mph", "23 planes"
  if (word_numeric(s + b.at, b.len))
    return num ? 1 : 0;
  if (num && next >= 'a' && next <= 'z')
    return -1;
  // Just after a score: "DAL 27 | NYG 20"
  return num ? 3 : 2;
}

// Each item in pages that fit, each ending '\n'
__attribute__((noinline)) inline std::string ticker_pages(const std::string &line, int max_chars,
                                                         int per_item = kPagesPerItem) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  if (max_chars < 4)
    max_chars = 4;
  const char *s = line.c_str();
  const size_t n = line.size();
  std::vector<PageWord> w;
  auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); };
  auto emit = [&](size_t a, size_t b, bool more, bool dots, int last_chars) {
    if (more)
      out += kPageMore;
    uint32_t cur = 0;
    for (size_t k = a; k <= b; k++) {
      const PageWord &p = w[k];
      if (p.rgb != cur && cur != 0) {
        out += kRecolorCmd;
        cur = 0;
      }
      if (k > a)
        out += ' ';
      if (p.rgb != cur) {
        out += kRecolorCmd;
        for (int sh = 20; sh >= 0; sh -= 4)
          out += kHex[(p.rgb >> sh) & 0xF];
        out += ' ';
        cur = p.rgb;
      }
      out.append(s + p.at, k == b && last_chars >= 0 ? utf8_bytes(s + p.at, p.len, last_chars) : p.len);
    }
    if (cur != 0)
      out += kRecolorCmd;
    if (dots)
      out += "..";
    out += '\n';
  };
  auto chars = [&](size_t a, size_t b) {
    int c = (int) (b - a);
    for (size_t k = a; k <= b; k++)
      c += w[k].chars;
    return c;
  };
  auto paginate = [&]() {
    size_t i = 0;
    for (int pages = 0; i < w.size(); pages++) {
      const bool more = pages > 0;
      size_t j = i;
      while (j + 1 < w.size() && chars(i, j + 1) <= max_chars)
        j++;
      if (j + 1 < w.size() && pages + 1 == per_item) {
        // The last page ends "..", marking the rest left out
        j = i;
        while (j + 1 < w.size() && chars(i, j + 1) <= max_chars - 2)
          j++;
        emit(i, j, more, true, w[i].chars > max_chars - 2 ? max_chars - 2 : -1);
        break;
      }
      if (j + 1 < w.size()) {
        size_t best = j;
        int score = break_score(s, w[j], w[j + 1]);
        for (size_t k = j; k-- > i && chars(i, k) * 3 >= max_chars;)
          if (break_score(s, w[k], w[k + 1]) > score) {
            best = k;
            score = break_score(s, w[k], w[k + 1]);
          }
        j = best;
      }
      emit(i, j, more, false, -1);
      i = j + 1;
    }
    w.clear();
  };
  uint32_t rgb = 0;
  bool span = false;
  size_t i = 0;
  while (i < n) {
    const char c = s[i];
    if (c == kRecolorCmd) {
      if (!span && i + 7 < n && s[i + 7] == ' ' && hex(s[i + 1]) && hex(s[i + 2]) && hex(s[i + 3]) &&
          hex(s[i + 4]) && hex(s[i + 5]) && hex(s[i + 6])) {
        rgb = (uint32_t) strtoul(std::string(s + i + 1, 6).c_str(), nullptr, 16);
        span = true;
        i += 8;
      } else {
        span = false;
        rgb = 0;
        i++;
      }
      continue;
    }
    if (c == ' ' || c == '\n' || c == kPageMore) {
      i++;
      continue;
    }
    size_t j = i;
    while (j < n && s[j] != ' ' && s[j] != '\n' && s[j] != kPageMore && s[j] != kRecolorCmd)
      j++;
    if (!span && j - i == 1 && c == '|') {
      paginate();
    } else {
      // A word too long for a page is cut
      for (size_t k = i; k < j;) {
        const size_t b = utf8_bytes(s + k, j - k, max_chars);
        PageWord p{(uint32_t) k, (uint32_t) b, span ? rgb : 0, 0};
        p.chars = utf8_count(std::string(s + k, b));
        w.push_back(p);
        k += b;
      }
    }
    i = j;
  }
  paginate();
  return out;
}

inline size_t page_count(const std::string &pages) {
  size_t c = 0;
  for (char ch : pages)
    c += ch == '\n' ? 1 : 0;
  return c;
}

// Page i, without its carry-on mark
inline std::string page_at(const std::string &pages, size_t i, bool *more = nullptr) {
  size_t at = 0;
  for (; i > 0 && at < pages.size(); i--)
    at = pages.find('\n', at) + 1;
  const bool m = at < pages.size() && pages[at] == kPageMore;
  if (more != nullptr)
    *more = m;
  at += m ? 1 : 0;
  const size_t end = pages.find('\n', at);
  return at < pages.size() ? pages.substr(at, end - at) : std::string();
}

// Paged: still pages; else the usual scrolling line
inline bool area_ticker_paged(const TickerSeam &s, bool clock) { return s.on && !clock; }

inline size_t ticker_page_next(size_t page, size_t pages) { return pages == 0 ? 0 : (page + 1) % pages; }

}  // namespace espn
