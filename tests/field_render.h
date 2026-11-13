// Field view review images, drawn as the panel would
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "field.h"

namespace espn {

struct BdfGlyph {
  int dw{0}, w{0}, h{0}, xo{0}, yo{0};
  std::vector<uint32_t> rows;
};

// One spleen BDF, as the YAML's fonts use them
struct BdfFont {
  int ascent{0};
  std::map<int, BdfGlyph> g;
  bool ok() const { return !g.empty(); }
};

inline BdfFont bdf_load(const std::string &path) {
  BdfFont f;
  std::ifstream in(path);
  std::string line;
  BdfGlyph cur;
  int code = -1;
  bool bits = false;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string k;
    ss >> k;
    if (k == "FONT_ASCENT")
      ss >> f.ascent;
    else if (k == "ENCODING")
      ss >> code;
    else if (k == "DWIDTH")
      ss >> cur.dw;
    else if (k == "BBX")
      ss >> cur.w >> cur.h >> cur.xo >> cur.yo;
    else if (k == "BITMAP")
      bits = true;
    else if (k == "ENDCHAR") {
      if (code >= 0)
        f.g[code] = cur;
      cur = BdfGlyph();
      bits = false;
    } else if (bits)
      cur.rows.push_back((uint32_t) strtoul(k.c_str(), nullptr, 16));
  }
  return f;
}

// S8 to S32, from REVIEW_FONTS or dir; empty draws boxes
struct ReviewFonts {
  BdfFont f[5];
};

inline ReviewFonts review_fonts(const std::string &dir) {
  ReviewFonts r;
  static const char *const kFiles[] = {"spleen-5x8.bdf", "spleen-6x12.bdf", "spleen-8x16.bdf", "spleen-12x24.bdf",
                                       "spleen-16x32.bdf"};
  for (int i = 0; i < 5 && !dir.empty(); i++)
    r.f[i] = bdf_load(dir + "/" + kFiles[i]);
  return r;
}

inline int bdf_width(const BdfFont &f, const std::string &s) {
  int w = 0;
  for (unsigned char c : s) {
    auto it = f.g.find(c);
    w += it != f.g.end() ? it->second.dw : 0;
  }
  return w;
}

struct Canvas {
  std::vector<uint16_t> px;
  int w{0}, h{0};
  void put(int x, int y, uint16_t c) {
    if (x >= 0 && y >= 0 && x < w && y < h)
      px[(size_t) y * w + x] = c;
  }
};

// A label: clipped to its width, aligned in it
inline void review_text(Canvas &cv, const ReviewFonts &fonts, const AppText &t) {
  const int fi = (int) t.font;
  const BdfFont &f = fonts.f[fi];
  const uint16_t c = rgb565(t.color);
  const int rows = esphome::gameday::font_rows(t.font);
  if (!f.ok()) {
    const int tw = std::min<int>(t.w, (int) t.text.size() * esphome::gameday::font_px(t.font));
    const int x0 = t.align == AppAlign::LEFT ? t.x : t.align == AppAlign::RIGHT ? t.x + t.w - tw : t.x + (t.w - tw) / 2;
    for (int x = x0; x < x0 + tw; x++) {
      cv.put(x, t.y, c);
      cv.put(x, t.y + rows - 1, c);
    }
    return;
  }
  const int tw = bdf_width(f, t.text);
  int x = t.align == AppAlign::LEFT ? t.x : t.align == AppAlign::RIGHT ? t.x + t.w - tw : t.x + (t.w - tw) / 2;
  if (t.align != AppAlign::LEFT && tw > t.w)
    x = t.x;
  const int base = t.y + f.ascent;
  for (unsigned char ch : t.text) {
    auto it = f.g.find(ch);
    if (it == f.g.end())
      continue;
    const BdfGlyph &g = it->second;
    const int top = base - g.yo - g.h;
    const int bytes = (g.w + 7) / 8;
    for (int r = 0; r < g.h && r < (int) g.rows.size(); r++)
      for (int b = 0; b < g.w; b++)
        if ((g.rows[r] >> (8 * bytes - 1 - b)) & 1) {
          const int px = x + g.xo + b;
          if (px >= t.x && px < t.x + t.w)
            cv.put(px, top + r, c);
        }
    x += g.dw;
  }
}

// The board: picture, labels, then the ticker's first screen
inline Canvas review_frame(int w, const SportFrame &fr, const ReviewFonts &fonts, const std::string &ticker,
                           bool large) {
  Canvas cv;
  cv.w = w;
  cv.h = esphome::gameday::kBoardRows;
  cv.px.assign((size_t) cv.w * cv.h, 0);
  const FieldPic &pic = fr.pic;
  std::vector<uint16_t> buf((size_t) pic.w * pic.h);
  Px565 p;
  p.px = buf.data();
  p.w = p.stride = pic.w;
  p.h = pic.h;
  p.clip = PxBox{0, 0, pic.w, pic.h};
  field_paint(p, pic);
  for (int y = 0; y < pic.h; y++)
    for (int x = 0; x < pic.w; x++)
      cv.put(pic.x + x, pic.y + y, buf[(size_t) y * pic.w + x]);
  for (const AppText &t : fr.texts)
    review_text(cv, fonts, t);
  AppText tk;
  tk.x = 0;
  tk.w = (int16_t) w;
  tk.font = large ? esphome::gameday::BoardFont::S12 : esphome::gameday::BoardFont::S8;
  tk.y = (int16_t) (cv.h - esphome::gameday::font_rows(tk.font));
  tk.text = ticker;
  review_text(cv, fonts, tk);
  return cv;
}

}  // namespace espn
