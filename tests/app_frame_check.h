// What any display's frame must keep to, whatever its size.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "apps.h"
#include "board_layout.h"

// Problems found, one line each; none means it fits.
inline std::vector<std::string> app_frame_problems(const espn::AppFrame &f, int w, int h) {
  using namespace esphome::gameday;
  std::vector<std::string> out;
  auto say = [&out](const char *fmt, const std::string &what, int a = 0, int b = 0) {
    char buf[200];
    snprintf(buf, sizeof buf, fmt, what.c_str(), a, b);
    out.push_back(buf);
  };
  const int limit = h - 8;  // the ticker's row
  if (f.texts.size() > espn::kAppTexts || f.icons.size() > espn::kAppIcons || f.art.size() > espn::kAppArt)
    say("%s: %d labels, %d logos", "too many", (int) f.texts.size(), (int) f.icons.size());
  if (tall_board(w, h) && !f.rows.name.empty())
    say("%s: %d rows for the 64-row board", "tall", (int) f.rows.name.size());
  struct Box {
    int x0, x1, y0, y1;
  };
  auto apart = [](const Box &a, const Box &b) { return a.x1 <= b.x0 || b.x1 <= a.x0 || a.y1 < b.y0 || b.y1 < a.y0; };
  std::vector<Box> ink;
  for (const espn::AppText &t : f.texts) {
    size_t n = 0;
    for (char c : t.text)
      n += ((unsigned char) c & 0xC0) != 0x80 ? 1 : 0;
    const int px = (int) n * font_px(t.font);
    if (px > t.w)
      say("\"%s\" is %d px in %d", t.text, px, t.w);
    if (t.x < 0 || t.x + t.w > w)
      say("\"%s\" outside: x %d w %d", t.text, t.x, t.w);
    const int x0 = t.align == espn::AppAlign::LEFT    ? t.x
                   : t.align == espn::AppAlign::RIGHT ? t.x + t.w - px
                                                      : t.x + (t.w - px) / 2;
    const Box b{x0, x0 + px, t.y + font_ink_top(t.font), t.y + font_desc_bottom(t.font)};
    if (!t.text.empty() && (b.y0 < 0 || b.y1 >= limit))
      say("\"%s\" off the board or on the ticker: rows %d-%d", t.text, b.y0, b.y1);
    for (size_t i = 0; i < ink.size(); i++)
      if (!t.text.empty() && !f.texts[i].text.empty() && !apart(b, ink[i]))
        say("\"%s\" overlap", t.text + "\" and \"" + f.texts[i].text);
    ink.push_back(b);
  }
  for (const espn::AppIcon &ic : f.icons) {
    const Box b{ic.x, ic.x + ic.px, ic.y, ic.y + ic.px - 1};
    if (b.x0 < 0 || b.x1 > w || b.y0 < 0 || b.y1 >= limit)
      say("%s: a logo at %d,%d outside", ic.key, ic.x, ic.y);
    for (size_t i = 0; i < ink.size(); i++)
      if ((int) i != ic.alt && !f.texts[i].text.empty() && !apart(b, ink[i]))
        say("%s: logo over a label", std::string(ic.key) + "'s logo and \"" + f.texts[i].text + "\"");
  }
  for (const espn::AppArt &a : f.art)
    if (a.x < 0 || a.x + a.w * a.scale > w || a.y < 0 || a.y + a.h * a.scale > limit)
      say("%s: at %d,%d outside", "a picture", a.x, a.y);
  if (f.photo.w > 0 && (f.photo.x < 0 || f.photo.x + f.photo.w > w || f.photo.y < 0 || f.photo.y + f.photo.h > limit))
    say("%s: at %d,%d outside", "the photo", f.photo.x, f.photo.y);
  for (const espn::AppPoint &p : f.line.pts)
    if (p.x < 0 || p.x >= w || p.y < 0 || p.y >= limit)
      say("%s: a point at %d,%d outside", "the line", p.x, p.y);
  if (f.bar.w > 0 && (f.bar.x < 0 || f.bar.x + f.bar.w > w || f.bar.y < 0 || f.bar.y + f.bar.h > limit))
    say("%s: at y %d, %d tall", "the bar", f.bar.y, f.bar.h);
  return out;
}
