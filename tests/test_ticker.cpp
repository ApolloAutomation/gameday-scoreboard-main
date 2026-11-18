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
      TCHECK(s.board_w == 128 - kSeamDark && s.board_w < a.x);
      TCHECK(a.x == 128 && a.w == (cols == 3 ? 64 : 128));
      // Dark, the divider, dark, then the tag
      TCHECK(s.line_x == 0 && s.tag_x == 1 + kSeamDark);
      TickerTag t = area_tag(AreaView::APP, show, "", 0);
      TCHECK(seam_text_x(s, t) >= s.tag_x);
      TCHECK(area_ticker_paged(s, false) && !area_ticker_paged(s, true));
    }
  // Two rows of two, Off, and a display filling the row: as before
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
        for (int x = 125; x < 128; x++)
          TCHECK(column_dark(cv, x, ty, 64));
        for (int x = 129; x < 132; x++)
          TCHECK(column_dark(cv, x, ty, 64));
        TCHECK(cv.px[(size_t) 63 * cv.w + 128] == rgb565(kSeamColor));
        TCHECK(cv.px[(size_t) ty * cv.w + 128] == rgb565(kSeamColor));
        TCHECK(column_dark(cv, 128, 0, ty));
      }
}

std::vector<std::string> split_pages(const std::string &line, int max_chars) {
  const std::string p = ticker_pages(line, max_chars);
  std::vector<std::string> out;
  for (size_t i = 0; i < page_count(p); i++)
    out.push_back(page_at(p, i));
  TCHECK(p.empty() || p.back() == '\n');
  return out;
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
  }
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
    TCHECK(g.color != 0x041E42);  // lifted off navy
  }
  TCHECK(std::string(area_tag(AreaView::TEAM, SplitShow::TEAM, "LONGNAME", 0).text) == "LONG");
  TCHECK(area_tag(AreaView::TEAM, SplitShow::TEAM, "", 0).icon == TagIcon::BALL);
  // Distinct icons, each drawn, none the board's white
  const TagIcon icons[] = {TagIcon::PLANE, TagIcon::CLOUD, TagIcon::CHART, TagIcon::FLAG, TagIcon::BALL};
  std::vector<std::vector<bool>> seen;
  for (TagIcon i : icons) {
    std::vector<bool> lit;
    int n = 0;
    for (int y = 0; y < kTagPx; y++)
      for (int x = 0; x < kTagPx; x++) {
        lit.push_back(tag_lit(i, x, y));
        n += tag_lit(i, x, y) ? 1 : 0;
      }
    TCHECK(n >= 8);
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
    TCHECK(tag_text_color(g.color) != 0xFFFFFF);
  }
}

}  // namespace

int run_ticker_tests(int &total) {
  test_seam_layout();
  test_seam_render();
  test_pages();
  test_paged_render();
  test_tags();
  printf("ticker: %d checks, %d failures\n", checks, failures);
  total += checks;
  return failures;
}
