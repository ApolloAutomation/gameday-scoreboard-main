// The Stocks & Crypto display: list edits, failover, frames, state.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "app_markets.h"
#include "board_layout.h"
#include "source_markets.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

// One read as run_app_job_ makes it.
bool read_body(MarketsApp &a, const std::string &body, int64_t now) {
  JsonDocument filter, doc;
  return a.filter(filter) &&
         !deserializeJson(doc, body, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40)) &&
         a.parse_doc(doc.as<JsonObjectConst>(), now);
}

#define KCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// The fixtures were read at 2026-11-12 04:07:19 UTC.
constexpr int64_t kNow = 1794456439;

std::string list_text(const MarketBlob &b) {
  std::string out;
  for (uint8_t i = 0; i < market_count(b); i++)
    out += (out.empty() ? "" : ",") + std::string(b.syms[i].sym) + ":" + market_kind_key((MarketKind) b.syms[i].kind);
  return out;
}

MarketSymbol sym(const char *text) {
  MarketSymbol s{};
  parse_market_symbol(text, MarketKind::NONE, s);
  return s;
}

// A time showing the nth charted symbol's sparkline.
int64_t card_at(int n) {
  const int64_t tick = kNow / kMarketCardSeconds;
  return (tick - tick % kMarketSlots + n) * kMarketCardSeconds + 1;
}

void test_set() {
  const MarketBlob d = market_defaults();
  KCHECK(list_text(d) == "SPY:stock,AAPL:stock,BTC:crypto,ETH:crypto", "%s", list_text(d).c_str());
  MarketList l;
  KCHECK(list_text(market_shown(l.blob, d)) == list_text(d), "empty shows the defaults");
  KCHECK(markets_set(l, d, "mkt", "0,AAPL") == AppSet::SAME && market_count(l.blob) == 0 && !l.dirty,
         "a default again saves nothing");
  KCHECK(markets_set(l, d, "mkt", "0,tsla") == AppSet::CHANGED && l.dirty, "add");
  KCHECK(list_text(l.blob) == "SPY:stock,AAPL:stock,BTC:crypto,ETH:crypto,TSLA:stock", "%s",
         list_text(l.blob).c_str());
  KCHECK(markets_set(l, d, "mkt", "0,SOL,stock") == AppSet::CHANGED && l.blob.syms[5].kind == (uint8_t) MarketKind::STOCK,
         "a kind given");
  KCHECK(markets_set(l, d, "mkt", "0,^GSPC") == AppSet::CHANGED && markets_set(l, d, "mkt", "0,DOGE") == AppSet::CHANGED,
         "seven, eight");
  KCHECK(markets_set(l, d, "mkt", "0,XRP") == AppSet::REFUSED, "eight is the most");
  KCHECK(markets_set(l, d, "mkt", "2,MSFT") == AppSet::CHANGED && std::string(l.blob.syms[1].sym) == "MSFT",
         "a slot takes a new symbol");
  KCHECK(markets_set(l, d, "mkt", "0,AA PL!") == AppSet::REFUSED && markets_set(l, d, "mkt", "AAPL") == AppSet::REFUSED &&
             markets_set(l, d, "mkt", "0,AAPL,bond") == AppSet::REFUSED && markets_set(l, d, "mkt", "") == AppSet::REFUSED,
         "bad requests");
  KCHECK(markets_set(l, d, "mktmove", "8,1") == AppSet::CHANGED && std::string(l.blob.syms[0].sym) == "DOGE",
         "move to the top");
  KCHECK(markets_set(l, d, "mktmove", "3,3") == AppSet::SAME, "a move in place");
  KCHECK(markets_set(l, d, "mktmove", "9,1") == AppSet::REFUSED && markets_set(l, d, "mktmove", "1") == AppSet::REFUSED,
         "bad moves");
  KCHECK(markets_set(l, d, "mktdel", "1") == AppSet::CHANGED && market_count(l.blob) == 7, "delete");
  KCHECK(markets_set(l, d, "mktdel", "8") == AppSet::REFUSED && markets_set(l, d, "mktdel", "0") == AppSet::REFUSED,
         "past the list");
  KCHECK(markets_set(l, d, "other", "1") == AppSet::REFUSED, "not a markets key");
  while (market_count(l.blob) > 0)
    markets_set(l, d, "mktdel", "1");
  KCHECK(list_text(market_shown(l.blob, d)) == list_text(d), "an emptied list shows the defaults again");
  MarketList m;
  KCHECK(markets_set(m, d, "mktdel", "1") == AppSet::CHANGED, "delete a default");
  KCHECK(list_text(m.blob) == "AAPL:stock,BTC:crypto,ETH:crypto", "%s", list_text(m.blob).c_str());
}

void test_feed() {
  MarketBlob mixed{};
  for (const char *t : {"AAPL", "BTC", "^GSPC"})
    market_put(mixed, 0, sym(t));
  MarketFeed f;
  KCHECK(f.next(mixed, kNow) == MarketSource::YAHOO, "Yahoo first");
  KCHECK(!f.done(MarketSource::YAHOO, false, false, mixed, kNow) && !f.backup, "one failure waits");
  KCHECK(f.done(MarketSource::YAHOO, false, false, mixed, kNow) && f.backup && f.now, "two go to the backups now");
  KCHECK(f.retry_at == kNow + kMarketRetrySeconds, "Yahoo again in ten minutes");
  KCHECK(f.next(mixed, kNow) == MarketSource::KRAKEN, "coins from Kraken");
  KCHECK(f.done(MarketSource::KRAKEN, true, false, mixed, kNow) && f.now, "Nasdaq straight after");
  KCHECK(f.next(mixed, kNow) == MarketSource::NASDAQ, "stocks from Nasdaq");
  KCHECK(!f.done(MarketSource::NASDAQ, true, false, mixed, kNow) && !f.now, "then the usual wait");
  KCHECK(f.next(mixed, kNow + 60) == MarketSource::KRAKEN, "the next refresh");
  KCHECK(f.next(mixed, kNow + kMarketRetrySeconds) == MarketSource::YAHOO, "Yahoo's retry");
  // A failed retry holds longer and goes straight back.
  KCHECK(f.done(MarketSource::YAHOO, false, false, mixed, kNow + kMarketRetrySeconds), "back to the backups");
  KCHECK(f.retries == 1 && f.retry_at == kNow + 3 * kMarketRetrySeconds, "twenty minutes");
  KCHECK(!f.done(MarketSource::YAHOO, true, false, mixed, kNow + 4000) && !f.backup && f.fails == 0,
         "a good Yahoo read ends it");
  // A refusal moves at once and holds half an hour.
  KCHECK(f.done(MarketSource::YAHOO, false, true, mixed, kNow) && f.backup, "refused");
  KCHECK(f.retry_at == kNow + kMarketRefusedSeconds, "half an hour");
  // A refusing Kraken rests; Nasdaq still reads.
  KCHECK(f.done(MarketSource::KRAKEN, false, true, mixed, kNow) && f.next(mixed, kNow) == MarketSource::NASDAQ,
         "Kraken refused");
  f.done(MarketSource::NASDAQ, true, false, mixed, kNow);
  KCHECK(f.next(mixed, kNow + 60) == MarketSource::NASDAQ, "Kraken rests");
  KCHECK(f.next(mixed, kNow + kMarketRetrySeconds) == MarketSource::KRAKEN, "and comes back");
  // Nothing the backups carry: Yahoo it is.
  MarketBlob index{};
  market_put(index, 0, sym("^GSPC"));
  MarketFeed g;
  KCHECK(!g.done(MarketSource::YAHOO, false, true, index, kNow) && g.next(index, kNow) == MarketSource::YAHOO,
         "an index alone stays on Yahoo");
  MarketBlob coins{};
  market_put(coins, 0, sym("ETH"));
  MarketFeed h;
  h.done(MarketSource::YAHOO, false, true, coins, kNow);
  KCHECK(!h.done(MarketSource::KRAKEN, true, false, coins, kNow), "coins alone: no Nasdaq read");
}

void test_merge() {
  MarketBlob b{};
  for (const char *t : {"AAPL", "BTC"})
    market_put(b, 0, sym(t));
  MarketQuote y[kMarketSlots], k[kMarketSlots], n[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  KCHECK(parse_yahoo_quotes(json, b, y) == 2, "yahoo");
  json = slurp("fixtures/markets_kraken_ticker.json");
  KCHECK(parse_kraken_quotes(json, b, kNow + 600, k) == 1, "kraken");
  const MarketQuote btc = market_merge(y[1], k[1], kNow + 600);
  KCHECK(btc.price == k[1].price && market_spark_count(btc) >= market_spark_count(y[1]), "keeps the sparkline");
  KCHECK(btc.open_at == y[1].open_at, "on its session");
  const int at = market_spark_bucket(kNow + 600, btc.open_at, btc.close_at);
  KCHECK(at >= 0 && btc.spark[at] != kSparkEmpty, "with the new price");
  KCHECK(market_spark_count(market_merge(y[1], k[1], kNow + 2 * 86400)) == 0, "not the next day's");
  KCHECK(market_merge(y[1], y[1], kNow).price == y[1].price, "Yahoo's own stands");
  json = slurp("fixtures/markets_nasdaq_watchlist.json");
  KCHECK(parse_nasdaq_quotes(json, b, kNow, n) == 1, "nasdaq");
  // After 8 PM the session is over: Nasdaq's words rule.
  KCHECK(market_spark_count(market_merge(y[0], n[0], kNow)) == 0, "closed: no day to keep");
  const int64_t late = (int64_t) y[0].close_at + 600;
  const MarketQuote aapl = market_merge(y[0], n[0], late);
  KCHECK(market_spark_count(aapl) == kSparkPoints && aapl.open_at == y[0].open_at, "after hours keeps the day");
  KCHECK(market_state(aapl, MarketKind::STOCK, late) == MarketState::POST, "and its hours");
}

bool in_box(const AppFrame &f, int x0, int y0, int w, int h) {
  for (const AppPoint &p : f.line.pts)
    if (p.x < x0 || p.x >= x0 + w || p.y < y0 || p.y >= y0 + h)
      return false;
  return f.line.pts.size() >= 2;
}

void test_frames() {
  MarketBlob b{};
  for (const char *t : {"AAPL", "SPY", "^GSPC", "BRK.B", "BTC", "ETH", "DOGE", "SHIB"})
    market_put(b, 0, sym(t));
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  KCHECK(parse_yahoo_quotes(json, b, q) == 8, "eight");

  // Two panels: symbol, price, percent; four rows over the line.
  AppFrame f;
  markets_frame(b, q, 128, 64, card_at(0), f);
  app_frame_trim(f);
  KCHECK(f.rows.cols == std::vector<int16_t>({0, 0, 0, 30, 30, 50, 80, 48}), "128 columns");
  KCHECK(f.rows.name.size() == 4 && f.rows.name[0] == "AAPL" && f.rows.name[3] == "BRK.B", "page one");
  KCHECK(f.rows.title == "MARKETS 1/2" && f.rows.status == "CLOSED", "%s | %s", f.rows.title.c_str(),
         f.rows.status.c_str());
  KCHECK(f.rows.value[0] == "355.63" && f.rows.detail[0] == "+2.04%", "%s %s", f.rows.value[0].c_str(),
         f.rows.detail[0].c_str());
  KCHECK(f.rows.detail_color[0] == (int) kMarketUp && f.rows.value_color[0] == (int) kMarketFlat, "colors");
  KCHECK(f.rows.name_color[0] == (int) kMarketFlat && f.rows.name_color[1] == (int) kMarketDim, "AAPL is charted");
  KCHECK(in_box(f, 0, 41, 128, 14) && f.line.color == kMarketUp, "its line under the rows");
  KCHECK(f.ticker.find("AAPL 355.63 +7.12 +2.04%") == 0 && !f.spans.empty(), "%s", f.ticker.c_str());
  AppFrame later;
  markets_frame(b, q, 128, 64, card_at(5), later);
  KCHECK(later.rows.name.size() == 4 && later.rows.name[0] == "BTC" && later.rows.title == "MARKETS 2/2",
         "the line moves on and its page with it");
  KCHECK(later.rows.name_color[1] == (int) kMarketFlat && later.rows.name_color[0] == (int) kMarketDim, "ETH's line");

  // One panel: price, then percent.
  AppFrame one, flip;
  const int64_t t = card_at(0);
  const int64_t even = t - (t / kMarketAltSeconds) % 2 * kMarketAltSeconds;
  markets_frame(b, q, 64, 64, even, one);
  markets_frame(b, q, 64, 64, even + kMarketAltSeconds, flip);
  KCHECK(one.rows.cols == std::vector<int16_t>({0, 0, 0, 30, 30, 34, 0, 0}), "64 columns");
  KCHECK(one.rows.value[0] == "355.63" && flip.rows.value[0] == "+2.04%", "%s then %s", one.rows.value[0].c_str(),
         flip.rows.value[0].c_str());
  KCHECK(one.rows.value_color[0] == (int) kMarketUp && one.rows.title.empty() && one.rows.status == "CLOSED",
         "colored value, status alone");
  KCHECK(in_box(one, 0, 41, 64, 14), "line");

  // Three panels: change too; four: the line beside six rows.
  AppFrame three, four;
  markets_frame(b, q, 192, 64, card_at(0), three);
  KCHECK(three.rows.cols == std::vector<int16_t>({41, 55, 1, 40, 96, 50, 146, 45}), "192 columns");
  KCHECK(three.rows.pos[0] == "355.63" && three.rows.value[0] == "+7.12" && three.rows.detail[0] == "+2.04%",
         "price, change, percent");
  KCHECK(three.rows.pos_color[0] == (int) kMarketFlat && three.rows.value_color[0] == (int) kMarketUp, "colors");
  markets_frame(b, q, 256, 64, card_at(7), four);
  app_frame_trim(four);
  KCHECK(four.rows.name.size() == 2 && four.rows.name[1] == "SHIB" && four.rows.title == "MARKETS 2/2",
         "six a page beside the line");
  KCHECK(in_box(four, 194, 9, 60, 46), "the line at the side");

  // Nothing charted: six rows, pages in turn.
  MarketQuote flat[kMarketSlots];
  for (int i = 0; i < kMarketSlots; i++) {
    flat[i] = q[i];
    memset(flat[i].spark, kSparkEmpty, sizeof flat[i].spark);
  }
  AppFrame p0, p1;
  markets_frame(b, flat, 128, 64, card_at(0) - (card_at(0) / kMarketCardSeconds) % 2 * kMarketCardSeconds, p0);
  markets_frame(b, flat, 128, 64, card_at(0) - (card_at(0) / kMarketCardSeconds) % 2 * kMarketCardSeconds +
                                       kMarketCardSeconds,
                p1);
  KCHECK(p0.rows.name.size() == 6 && p1.rows.name.size() == 2 && p0.line.pts.empty(), "pages without a line");
  KCHECK(p0.rows.name_color[1] == (int) kMarketFlat, "no row stands out");

  // Not read yet.
  MarketQuote none[kMarketSlots];
  AppFrame empty;
  markets_frame(b, none, 128, 64, kNow, empty);
  KCHECK(empty.rows.value[0] == "--" && empty.ticker.empty() && empty.rows.name.size() == 6, "dashes");

  // The board's cells: a display's own, or the leaderboard's.
  using esphome::gameday::rows_cells;
  const auto own = rows_cells(128, true, f.rows.cols);
  KCHECK(own.own && own.cell[0].w == 0 && own.cell[3].x == 80 && own.cell[3].w == 48, "own columns");
  const auto golf = rows_cells(64, true, {});
  KCHECK(!golf.own && golf.cell[1].w == 35 && golf.cell[2].w == 15 && golf.cell[3].w == 0, "golf on one panel");
  const auto racing = rows_cells(64, false, {});
  KCHECK(racing.cell[1].w == 52 && racing.cell[2].w == 0, "racing's names take the value's room");
  KCHECK(rows_cells(256, true, {}).cell[3].x == 64 + 94, "centered rows");
}

void test_app() {
  MarketsApp a;
  a.setup();
  KCHECK(list_text(a.symbols()) == "SPY:stock,AAPL:stock,BTC:crypto,ETH:crypto", "defaults");
  KCHECK(std::string(a.hosts()[0]) == "query1.finance.yahoo.com" && a.hosts()[3] == nullptr, "hosts");
  KCHECK(a.set("mkt", "0,BRK.B") == AppSet::CHANGED, "add");
  AppRequest req;
  KCHECK(a.request(kNow, req) && req.url == market_spark_url(a.symbols()), "%s", req.url.c_str());
  KCHECK(req.headers.size() == 1 && req.headers[0].name == "User-Agent" && req.headers[0].value == "Mozilla/5.0",
         "a browser's agent");
  const std::string yahoo_body = slurp("fixtures/markets_yahoo_spark.json");
  KCHECK(read_body(a, yahoo_body, kNow) && !a.quote(0).valid, "nothing shows before apply");
  a.apply(true, kNow);
  KCHECK(a.quote(0).valid && a.quote(4).valid && a.poll_ms(kNow) == kMarketPollMs, "read; coins every minute");
  // Busy in a trading day, never for coins alone
  const int64_t open = a.quote(1).open_at;
  KCHECK(!a.busy(kNow) && a.busy(open + 60) && !a.busy(open - 60), "the trading day");

  JsonDocument doc;
  a.state(doc["markets"].to<JsonObject>());
  JsonObjectConst m = doc["markets"];
  KCHECK(m["syms"].size() == 5 && std::string(m["syms"][1]["sym"] | "") == "AAPL", "listed");
  KCHECK(std::string(m["syms"][1]["price"] | "") == "355.63" && std::string(m["syms"][1]["pct"] | "") == "+2.04%" &&
             m["syms"][1]["dir"].as<int>() == 1 && std::string(m["syms"][1]["st"] | "") == "closed",
         "a quote");
  KCHECK(std::string(m["syms"][2]["st"] | "") == "always" && m["defaults"] == false && m["max"] == 8 &&
             std::string(m["src"] | "") == "yahoo" && m["read"].as<int64_t>() == kNow,
         "the rest");

  // Quotes follow their symbols.
  KCHECK(a.set("mktdel", "1") == AppSet::CHANGED && std::string(a.symbols().syms[0].sym) == "AAPL" &&
             a.quote(0).price > 355 && a.quote(0).price < 356,
         "AAPL's price moved up with it");
  KCHECK(a.set("mktmove", "4,1") == AppSet::CHANGED && std::string(a.symbols().syms[0].sym) == "BRK.B" &&
             a.quote(0).price > 580 && a.quote(1).price > 355 && a.quote(1).price < 356,
         "and moves with it");
  KCHECK(a.set("mkt", "0,ZZZZQ") == AppSet::CHANGED && !a.quote(4).valid, "a new symbol, not read");

  // Yahoo fails twice: Kraken, then Nasdaq, now.
  KCHECK(a.request(kNow + 60, req), "read");
  KCHECK(!a.failed(false) && !a.feed().backup, "once");
  a.apply(false, kNow + 60);
  AppFrame f;
  a.frame(128, 64, kNow + 60, f);
  KCHECK(!f.ticker.empty() && f.ticker.find("Can't") == std::string::npos, "prices stay up");
  KCHECK(a.request(kNow + 120, req) && a.failed(false) && a.poll_ms(kNow + 120) == 0, "twice: the backups now");
  KCHECK(a.request(kNow + 121, req) && req.url == "https://api.kraken.com/0/public/Ticker?pair=XXBTZUSD,XETHZUSD",
         "%s", req.url.c_str());
  const std::string kraken_body = slurp("fixtures/markets_kraken_ticker.json");
  KCHECK(read_body(a, kraken_body, kNow + 121), "kraken");
  a.apply(true, kNow + 121);
  KCHECK(a.quote(2).price > 73746 && a.quote(2).price < 73747 && market_spark_count(a.quote(2)) >= 11,
         "BTC from Kraken, its line kept");
  KCHECK(a.poll_ms(kNow + 121) == 0, "Nasdaq straight after");
  KCHECK(a.request(kNow + 122, req) && req.url.find("https://api.nasdaq.com/") == 0 &&
             req.url.find("brk.b%7Cstocks") != std::string::npos && req.url.find("zzzzq") != std::string::npos,
         "%s", req.url.c_str());
  const std::string nasdaq_body = slurp("fixtures/markets_nasdaq_watchlist.json");
  KCHECK(read_body(a, nasdaq_body, kNow + 122), "nasdaq");
  a.apply(true, kNow + 122);
  KCHECK(a.poll_ms(kNow + 122) == kMarketPollMs && a.feed().backup, "then the usual minute");
  doc.clear();
  a.state(doc["markets"].to<JsonObject>());
  KCHECK(std::string(doc["markets"]["src"] | "") == "backup" && doc["markets"]["syms"][4]["miss"].isNull(),
         "on the backups; only Yahoo says a symbol is unknown");
  KCHECK(a.request(kNow + 120 + kMarketRetrySeconds, req) && req.url.find("query1.finance.yahoo.com") != std::string::npos,
         "Yahoo again later");
  const std::string again_body = slurp("fixtures/markets_yahoo_spark.json");
  KCHECK(read_body(a, again_body, kNow + 720), "yahoo back");
  a.apply(true, kNow + 720);
  doc.clear();
  a.state(doc["markets"].to<JsonObject>());
  KCHECK(std::string(doc["markets"]["src"] | "") == "yahoo" && doc["markets"]["syms"][4]["miss"] == true &&
             !a.feed().backup,
         "Yahoo answers without ZZZZQ");

  // Never read: the ticker says why.
  MarketsApp b;
  b.setup();
  AppFrame idle;
  b.frame(64, 64, kNow, idle);
  KCHECK(idle.ticker == "Reading prices" && idle.rows.value[0] == "--", "%s", idle.ticker.c_str());
  b.request(kNow, req);
  b.failed(true);
  b.apply(false, kNow);
  AppFrame down;
  b.frame(64, 64, kNow, down);
  KCHECK(down.ticker == "Can't reach Yahoo Finance, Kraken or Nasdaq", "%s", down.ticker.c_str());
  const std::string junk_body = "<html>Edge: Too Many Requests</html>";
  b.request(kNow, req);
  KCHECK(!read_body(b, junk_body, kNow), "not JSON");
}

}  // namespace

int run_markets_app_tests(int &total_checks) {
  test_set();
  test_feed();
  test_merge();
  test_frames();
  test_app();
  total_checks += checks;
  return failures;
}
