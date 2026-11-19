#include <cstdio>
#include <string>
#include <vector>

#include "ticker_render.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define TCHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

const SplitShow kSides[] = {SplitShow::LIVE,   SplitShow::TEAM,    SplitShow::SCORES,  SplitShow::GAMEDAY,
                            SplitShow::F1,     SplitShow::MARKETS, SplitShow::FLIGHTS, SplitShow::WEATHER};

const char *const kBoard = "Q3 8:12 | 2nd & 7 at DAL 34 | Prescott 18/26 211 yds 2 TD | Lamb 7 rec 96 yds | O/U 47.5";
const char *const kFlights =
    "23 planes nearby | JBU1407 JetBlue, Airbus A321neo, JFK to CDG, 1,525 ft, 203 mph, 1.5 mi SE | LOT5N LOT, "
    "Boeing 777-200, JFK to WAW, 4,500 ft, 244 mph, 5.7 mi E";

void test_seam_layout() {
  for (int cols = 3; cols <= 4; cols++)
    for (SplitShow show : kSides) {
      const SplitArea a = split_area(cols, 1, (uint8_t) show);
      const TickerSeam s = ticker_seam(a);
      TCHECK(s.on);
      // The board's ticker stops short of the area
      TCHECK(s.board_w == 128 - kSeamDark && s.board_w < a.x && s.main_w == 128);
      TCHECK(a.x == 128 && a.w == (cols == 3 ? 64 : 128));
      // Dark, the divider, dark, then the tag
      TCHECK(s.line_x == 0 && s.tag_x == kSeamLinePx + kSeamPad);
      TCHECK(kSeamDark >= 5 && kSeamLinePx == 2);
      TickerTag t = area_tag(AreaView::APP, show, "", 0);
      TCHECK(seam_text_x(s, t) >= s.tag_x);
      TCHECK(area_ticker_paged(s, false) && !area_ticker_paged(s, true));
    }
  // Two rows of two, Off, or one display: unchanged
  for (SplitShow show : kSides) {
    const SplitArea b = split_area(2, 2, (uint8_t) show);
    TCHECK(b.on && !ticker_seam(b).on && ticker_seam(b).board_w == 128);
    TCHECK(!area_ticker_paged(ticker_seam(b), false));
    for (int cols = 3; cols <= 4; cols++) {
      const SplitArea full = split_area(cols, 1, (uint8_t) show, true);
      TCHECK(!ticker_seam(full).on && ticker_seam(full).board_w == cols * 64);
    }
  }
  for (int cols = 1; cols <= 4; cols++) {
    const SplitArea off = split_area(cols, 1, 0);
    TCHECK(!ticker_seam(off).on && ticker_seam(off).board_w == cols * 64);
  }
}

bool column_dark(const Canvas &cv, int x, int y0, int y1) {
  for (int y = y0; y < y1; y++)
    if (cv.px[(size_t) y * cv.w + x] != 0)
      return false;
  return true;
}

void test_seam_render() {
  const ReviewFonts none;
  for (int cols = 3; cols <= 4; cols++)
    for (bool large : {false, true})
      for (int scroll = 0; scroll < 600; scroll += 7) {
        TickerShot s;
        s.cols = cols;
        s.board = kBoard;
        s.area = kFlights;
        s.large = large;
        s.scroll = scroll;
        s.ms = (uint32_t) scroll * 97;
        const Canvas cv = ticker_render(s, none);
        const int ty = 64 - (large ? 12 : 8);
        // Nothing in the gap either side of the divider
        for (int x = 128 - kSeamDark; x < 128; x++)
          TCHECK(column_dark(cv, x, ty, 64));
        for (int x = 128 + kSeamLinePx; x < 128 + kSeamLinePx + kSeamPad; x++)
          TCHECK(column_dark(cv, x, ty, 64));
        // Two columns, the board ticker's full height
        for (int x = 128; x < 128 + kSeamLinePx; x++) {
          TCHECK(cv.px[(size_t) 63 * cv.w + x] == rgb565(kSeamColor));
          TCHECK(cv.px[(size_t) ty * cv.w + x] == rgb565(kSeamColor));
          TCHECK(column_dark(cv, x, 0, ty));
        }
        // The area's words stay small at Large
        for (int x = 128 + kSeamLinePx; x < cv.w; x++)
          TCHECK(column_dark(cv, x, ty, 56));
      }
}

std::vector<std::string> split_pages(const std::string &line, int max_chars, std::vector<bool> *more = nullptr) {
  const std::string p = ticker_pages(line, max_chars);
  std::vector<std::string> out;
  for (size_t i = 0; i < page_count(p); i++) {
    bool m = false;
    out.push_back(page_at(p, i, &m));
    if (more != nullptr)
      more->push_back(m);
  }
  TCHECK(p.empty() || p.back() == '\n');
  return out;
}

// Letters only, markup left out
int shown_chars(const std::string &page) {
  int n = 0;
  for (size_t i = 0; i < page.size(); i++) {
    if (page[i] == kRecolorCmd) {
      i += i + 7 < page.size() && page[i + 7] == ' ' ? 7 : 0;
      continue;
    }
    n += ((unsigned char) page[i] & 0xC0) != 0x80 ? 1 : 0;
  }
  return n;
}

void test_page_breaks() {
  // A score's team and points stay together
  std::vector<bool> m;
  const std::vector<std::string> s = split_pages("DAL 27 NYG 20 | BUF 10 MIA 10", 9, &m);
  TCHECK(s.size() == 4 && s[0] == "DAL 27" && s[1] == "NYG 20" && s[2] == "BUF 10" && s[3] == "MIA 10");
  // Only a carried-on page is marked
  TCHECK(m.size() == 4 && !m[0] && m[1] && !m[2] && m[3]);
  const std::vector<std::string> w = split_pages("Wind SW 8 mph", 9);
  TCHECK(w.size() == 2 && w[0] == "Wind" && w[1] == "SW 8 mph");
  const std::vector<std::string> j = split_pages("Jones 12/19 141 yds", 9);
  TCHECK(j.size() == 3 && j[0] == "Jones" && j[1] == "12/19" && j[2] == "141 yds");
  // A number keeps its unit over its name
  const std::vector<std::string> bk = split_pages("Barkley 14 car 88 yds", 20);
  TCHECK(bk.size() == 2 && bk[0] == "Barkley 14 car" && bk[1] == "88 yds");
  const std::vector<std::string> f = split_pages("JFK to CDG, 1,525 ft, 203 mph", 9);
  for (const std::string &p : f) {
    TCHECK(p.find("1,525") == std::string::npos || p.find("ft") != std::string::npos);
    TCHECK(p.find("203") == std::string::npos || p.find("mph") != std::string::npos);
  }
  // A colored bet keeps its color on every page
  std::vector<bool> bm;
  const std::vector<std::string> b = split_pages("Q2 4:31 | `2ED573 BUF -3.5 COVERING` | O/U 44", 9, &bm);
  TCHECK(b.size() == 4 && b[0] == "Q2 4:31" && b[3] == "O/U 44");
  TCHECK(b.size() == 4 && b[1] == "`2ED573 BUF -3.5`" && b[2] == "`2ED573 COVERING`" && bm[2]);
  for (const std::string &p : b)
    TCHECK(shown_chars(p) <= 9);
  const std::vector<std::string> mix = split_pages("Lamb `FF4757 UNDER 6.5` rec", 30);
  TCHECK(mix.size() == 1 && mix[0] == "Lamb `FF4757 UNDER 6.5` rec");
}

void test_pages() {
  // Each item alone, every page fits
  const std::vector<std::string> p = split_pages(kFlights, 10);
  TCHECK(!p.empty() && p[0] == "23 planes");
  TCHECK(p[1] == "nearby");
  for (const std::string &s : p) {
    TCHECK(utf8_count(s) <= 10 && !s.empty());
    TCHECK(s.find(" | ") == std::string::npos && s.front() != ' ' && s.back() != ' ');
  }
  // Wide enough: one page per item
  const std::vector<std::string> w = split_pages("AAPL 231.40 +1.2% | BTC 98,120 -0.4% | ETH 3,410", 23);
  TCHECK(w.size() == 3 && w[0] == "AAPL 231.40 +1.2%" && w[2] == "ETH 3,410");
  // An item past the cap ends cut, marked
  std::string longer;
  for (int i = 0; i < 40; i++)
    longer += "word" + std::to_string(i) + " ";
  const std::vector<std::string> c = split_pages(longer, 12);
  TCHECK((int) c.size() == kPagesPerItem);
  TCHECK(c.back().size() >= 2 && c.back().substr(c.back().size() - 2) == "..");
  TCHECK(utf8_count(c.back()) <= 12);
  // A word wider than a page is cut
  const std::vector<std::string> cut = split_pages("ABCDEFGHIJKLMNO", 6);
  TCHECK(cut.size() == 3 && cut[0] == "ABCDEF" && cut[2] == "MNO");
  // Accented letters count once
  const std::vector<std::string> u = split_pages("Mönchengladbach 2 Köln 1", 15);
  TCHECK(u.size() == 2 && u[0] == "Mönchengladbach" && u[1] == "2 Köln 1");
  TCHECK(ticker_pages("", 10).empty() && ticker_pages(" | ", 10).empty() && page_at("", 3).empty());
  TCHECK(page_at(ticker_pages("a b c d e f g", 4), 1) == "c d");
  TCHECK(ticker_page_next(0, 3) == 1 && ticker_page_next(2, 3) == 0 && ticker_page_next(5, 0) == 0);
}

void test_paged_render() {
  const ReviewFonts none;
  for (int cols = 3; cols <= 4; cols++) {
    TickerShot s;
    s.cols = cols;
    s.board = kBoard;
    s.area = kFlights;
    // The board's line moves; the area's holds still
    auto area_px = [&](int scroll, uint32_t ms) {
      s.scroll = scroll;
      s.ms = ms;
      const Canvas cv = ticker_render(s, none);
      std::vector<uint16_t> out;
      for (int y = 56; y < 64; y++)
        for (int x = 128; x < cv.w; x++)
          out.push_back(cv.px[(size_t) y * cv.w + x]);
      return out;
    };
    auto board_px = [&](int scroll) {
      s.scroll = scroll;
      const Canvas cv = ticker_render(s, none);
      std::vector<uint16_t> out;
      for (int y = 56; y < 64; y++)
        for (int x = 0; x < 128; x++)
          out.push_back(cv.px[(size_t) y * cv.w + x]);
      return out;
    };
    TCHECK(area_px(0, 0) == area_px(40, 1000));
    TCHECK(area_px(0, 0) == area_px(90, kTickerPageMs - 1));
    TCHECK(area_px(0, 0) != area_px(0, kTickerPageMs));
    TCHECK(board_px(0) != board_px(42));
    // Under the area's clock, the board runs full width
    TickerShot c = s;
    c.board = std::string(80, 'M');
    c.view = AreaView::CLOCK;
    c.scroll = 0;
    bool lit = false;
    const Canvas cc = ticker_render(c, none);
    for (int x = 128 - kSeamDark; x < 128; x++)
      lit = lit || !column_dark(cc, x, 56, 64);
    TCHECK(lit);
    c.view = AreaView::APP;
    const Canvas cp = ticker_render(c, none);
    for (int x = 128 - kSeamDark; x < 128; x++)
      TCHECK(column_dark(cp, x, 56, 64));
  }
  // A bet's span keeps its color beside the board
  TickerShot bet;
  bet.cols = 4;
  bet.show = SplitShow::TEAM;
  bet.view = AreaView::TEAM;
  bet.abbr = "BUF";
  bet.team = 0x00338D;
  bet.board = kBoard;
  bet.area = "`2ED573 BUF -3.5 COVERING`";
  const Canvas bc = ticker_render(bet, none);
  bool green = false;
  for (int x = 128; x < 256; x++)
    green = green || bc.px[(size_t) 60 * bc.w + x] == rgb565(ticker_dim(0x2ED573));
  TCHECK(green);
  // Two rows of two: the area's line still scrolls
  TickerShot b;
  b.cols = 2;
  b.rows = 2;
  b.board = kBoard;
  b.area = kFlights;
  b.scroll = 0;
  const Canvas b0 = ticker_render(b, none);
  b.scroll = 42;
  const Canvas b1 = ticker_render(b, none);
  TCHECK(b0.px != b1.px);
  TCHECK(!column_dark(b0, 127, 64 + 56, 128));
}

void test_tags() {
  TickerTag t = area_tag(AreaView::APP, SplitShow::FLIGHTS, "", 0);
  TCHECK(t.icon == TagIcon::PLANE && t.text[0] == '\0');
  TCHECK(area_tag(AreaView::APP, SplitShow::WEATHER, "", 0).icon == TagIcon::CLOUD);
  TCHECK(area_tag(AreaView::APP, SplitShow::MARKETS, "", 0).icon == TagIcon::CHART);
  TCHECK(area_tag(AreaView::APP, SplitShow::F1, "", 0).icon == TagIcon::FLAG);
  TCHECK(area_tag(AreaView::SCORES, SplitShow::SCORES, "", 0).icon == TagIcon::BALL);
  TCHECK(area_tag(AreaView::LIVE, SplitShow::LIVE, "KC", 0xE31837).icon == TagIcon::BALL);
  TCHECK(area_tag(AreaView::CLOCK, SplitShow::TEAM, "", 0).icon == TagIcon::NONE);
  TCHECK(area_tag(AreaView::DATE, SplitShow::CLOCK, "", 0).icon == TagIcon::NONE);
  // A second team's game, and Game Day: the abbreviation
  for (AreaView v : {AreaView::TEAM, AreaView::GAME, AreaView::DETAIL}) {
    const TickerTag g = area_tag(v, SplitShow::TEAM, "DAL", 0x041E42);
    TCHECK(std::string(g.text) == "DAL" && tag_width(g) == 15);
    TCHECK(g.color != 0x041E42 && ((g.color & 0xFF) == 0xFF));  // lifted off navy
    // A full space between the tag and the words
    TCHECK(seam_text_x(ticker_seam(split_area(4, 1, (uint8_t) SplitShow::TEAM)), g) ==
           kSeamLinePx + kSeamPad + 15 + kTagCharPx);
  }
  TCHECK(std::string(area_tag(AreaView::TEAM, SplitShow::TEAM, "LONGNAME", 0).text) == "LONG");
  // Accented letters whole, never cut mid-letter
  const TickerTag acc = area_tag(AreaView::TEAM, SplitShow::TEAM, "\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89", 0);
  TCHECK(std::string(acc.text) == "\xC3\x89\xC3\x89\xC3\x89\xC3\x89" && tag_width(acc) == 20);
  TCHECK(std::string(area_tag(AreaView::TEAM, SplitShow::TEAM, "\xC4\x8CSK", 0).text) == "CSK");
  TCHECK(area_tag(AreaView::TEAM, SplitShow::TEAM, "", 0).icon == TagIcon::BALL);
  // Distinct icons, each drawn, none the board's white
  const TagIcon icons[] = {TagIcon::PLANE, TagIcon::CLOUD, TagIcon::CHART,
                           TagIcon::FLAG,  TagIcon::BALL,  TagIcon::MORE};
  std::vector<std::vector<bool>> seen;
  for (TagIcon i : icons) {
    std::vector<bool> lit;
    int n = 0;
    for (int y = 0; y < kTagPx; y++)
      for (int x = 0; x < kTagPx; x++) {
        lit.push_back(tag_lit(i, x, y));
        n += tag_lit(i, x, y) ? 1 : 0;
      }
    TCHECK(n >= (i == TagIcon::MORE ? 3 : 8));
    for (const auto &o : seen)
      TCHECK(o != lit);
    seen.push_back(lit);
    uint8_t buf[kTagBytes];
    tag_paint(i, 0xFF0000, buf);
    TCHECK(buf[2 * kTagPx * kTagPx + 3 * kTagPx + 3] == (tag_lit(i, 3, 3) ? 0xFF : 0));
  }
  TCHECK(tag_lit(TagIcon::NONE, 0, 0) == false && tag_width(TickerTag{}) == 0);
  for (SplitShow show : kSides) {
    const TickerTag g = area_tag(show == SplitShow::LIVE     ? AreaView::LIVE
                                 : show == SplitShow::SCORES ? AreaView::SCORES
                                 : show == SplitShow::TEAM   ? AreaView::TEAM
                                 : show == SplitShow::GAMEDAY ? AreaView::GAME
                                                              : AreaView::APP,
                                 show, "NYG", 0x0B2265);
    TCHECK(tag_width(g) > 0);
    // Dimmer than the board's white
    const uint32_t c = tag_text_color(g.color);
    const uint32_t r = c >> 16, gg = (c >> 8) & 0xFF, b = c & 0xFF;
    TCHECK(r <= 0xD8 && gg <= 0xD8 && b <= 0xD8);
    // Each display's words tinted, never plain gray
    const uint32_t hi = std::max(r, std::max(gg, b)), lo = std::min(r, std::min(gg, b));
    TCHECK(hi - lo >= 0x30);
  }
}

}  // namespace

int run_ticker_tests(int &total) {
  test_seam_layout();
  test_seam_render();
  test_pages();
  test_page_breaks();
  test_paged_render();
  test_tags();
  printf("ticker: %d checks, %d failures\n", checks, failures);
  total += checks;
  return failures;
}
