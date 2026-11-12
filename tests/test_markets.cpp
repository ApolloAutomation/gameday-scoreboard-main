// Stocks & Crypto: symbols, sources, quotes, text and frames.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "markets_view.h"
#include "source_markets.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define MCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

#define MCHECK_EQ(a, b)                                                                                   \
  do {                                                                                                    \
    checks++;                                                                                             \
    const std::string va = (a), vb = (b);                                                                 \
    if (va != vb) {                                                                                       \
      failures++;                                                                                         \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                                     \
  } while (0)

#define MCHECK_NEAR(a, b, eps)                                                                      \
  do {                                                                                              \
    checks++;                                                                                       \
    const double va = (a), vb = (b);                                                                \
    if (!(std::fabs(va - vb) <= (eps))) {                                                           \
      failures++;                                                                                   \
      printf("FAIL %s:%d: %s: %.10g != %.10g\n", __FILE__, __LINE__, #a, va, vb);               \
    }                                                                                               \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// The fixtures were read at 2026-11-12 04:07:19 UTC.
constexpr int64_t kNow = 1794456439;
// Wednesday 2026-11-11's session, Eastern
constexpr int64_t kPre = 1794387600, kOpen = 1794407400, kClose = 1794430800, kPost = 1794445200;
constexpr int64_t kUtcDay = 1794441600;

MarketSymbol sym(const char *text, MarketKind kind = MarketKind::NONE) {
  MarketSymbol s{};
  if (!parse_market_symbol(text, kind, s))
    printf("note: %s did not parse\n", text);
  return s;
}

// The fixture's eight, in the order the spark read asked.
MarketBlob fixture_list() {
  MarketBlob b{};
  for (const char *t : {"AAPL", "SPY", "^GSPC", "BRK.B", "BTC", "ETH", "DOGE", "SHIB"})
    market_put(b, 0, sym(t));
  return b;
}

int filled(const MarketQuote &q) {
  int n = 0;
  for (uint8_t v : q.spark)
    n += v != kSparkEmpty;
  return n;
}

void test_symbols() {
  struct Case {
    const char *text;
    MarketKind kind;
    const char *saved;
    MarketKind is;
    const char *yahoo, *kraken, *nasdaq;
  };
  const Case cases[] = {
      {"aapl", MarketKind::NONE, "AAPL", MarketKind::STOCK, "AAPL", "", "aapl"},
      {" $tsla ", MarketKind::NONE, "TSLA", MarketKind::STOCK, "TSLA", "", "tsla"},
      {"^gspc", MarketKind::NONE, "GSPC", MarketKind::INDEX, "^GSPC", "", ""},
      {"SPX", MarketKind::NONE, "SPX", MarketKind::INDEX, "^GSPC", "", ""},
      {"ndx", MarketKind::NONE, "NDX", MarketKind::INDEX, "^NDX", "", "ndx"},
      {"^FTSE", MarketKind::NONE, "FTSE", MarketKind::INDEX, "^FTSE", "", ""},
      // Dow Inc. and Compass trade as DOW and COMP
      {"DOW", MarketKind::NONE, "DOW", MarketKind::STOCK, "DOW", "", "dow"},
      {"DOW", MarketKind::INDEX, "DOW", MarketKind::INDEX, "^DJI", "", ""},
      {"comp", MarketKind::INDEX, "COMP", MarketKind::INDEX, "^IXIC", "", "comp"},
      {"btc", MarketKind::NONE, "BTC", MarketKind::CRYPTO, "BTC-USD", "XXBTZUSD", ""},
      {"BTC-USD", MarketKind::NONE, "BTC", MarketKind::CRYPTO, "BTC-USD", "XXBTZUSD", ""},
      {"eth/usd", MarketKind::NONE, "ETH", MarketKind::CRYPTO, "ETH-USD", "XETHZUSD", ""},
      {"XBT", MarketKind::NONE, "BTC", MarketKind::CRYPTO, "BTC-USD", "XXBTZUSD", ""},
      {"matic", MarketKind::NONE, "POL", MarketKind::CRYPTO, "POL28321-USD", "POLUSD", ""},
      {"doge", MarketKind::NONE, "DOGE", MarketKind::CRYPTO, "DOGE-USD", "XDGUSD", ""},
      {"TON", MarketKind::NONE, "TON", MarketKind::CRYPTO, "TON11419-USD", "TONUSD", ""},
      // A coin's ticker as a stock: Emeren Group
      {"SOL", MarketKind::STOCK, "SOL", MarketKind::STOCK, "SOL", "", "sol"},
      {"hbar-usd", MarketKind::NONE, "HBAR", MarketKind::CRYPTO, "HBAR-USD", "", ""},
      {"HBAR", MarketKind::CRYPTO, "HBAR", MarketKind::CRYPTO, "HBAR-USD", "", ""},
      {"brk/b", MarketKind::NONE, "BRK.B", MarketKind::STOCK, "BRK-B", "", "brk.b"},
      {"BRK-B", MarketKind::NONE, "BRK.B", MarketKind::STOCK, "BRK-B", "", "brk.b"},
      {"brk.b", MarketKind::STOCK, "BRK.B", MarketKind::STOCK, "BRK-B", "", "brk.b"},
      {"shop.to", MarketKind::NONE, "SHOP.TO", MarketKind::STOCK, "SHOP.TO", "", ""},
      {"GC=F", MarketKind::NONE, "GC=F", MarketKind::STOCK, "GC=F", "", ""},
      {"EURUSD=X", MarketKind::NONE, "EURUSD=X", MarketKind::STOCK, "EURUSD=X", "", ""},
  };
  for (const Case &c : cases) {
    MarketSymbol s{};
    const bool ok = parse_market_symbol(c.text, c.kind, s);
    MCHECK(ok, "%s parses", c.text);
    if (!ok)
      continue;
    MCHECK_EQ(s.sym, c.saved);
    MCHECK(s.kind == (uint8_t) c.is, "%s kind %d", c.text, s.kind);
    MCHECK_EQ(market_yahoo_symbol(s), c.yahoo);
    MCHECK_EQ(market_kraken_pair(s), c.kraken);
    MCHECK_EQ(market_nasdaq_symbol(s), c.nasdaq);
    MCHECK(market_symbol_valid(s), "%s valid", c.text);
  }
  MarketSymbol s{};
  for (const char *bad : {"", " ", "$", "^", "AAPL!", "ABCDEFGHIJK", ".AB", "AB.", "A..B", "A-", "A B C D E F G H I J K L",
                          "\xC3\x84PPLE", "-USD", "AB,C", "A%20B", "^BRK.B"})
    MCHECK(!parse_market_symbol(bad, MarketKind::NONE, s), "\"%s\" refused", bad);
  MCHECK(!parse_market_symbol("^BTC", MarketKind::CRYPTO, s), "a caret is an index");
  MCHECK(!parse_market_symbol("BTC-USD", MarketKind::INDEX, s), "an index has no dash");
  MCHECK(!parse_market_symbol("BRK.B", MarketKind::CRYPTO, s), "a coin has no dot");
  MCHECK(parse_market_symbol("BTC-USD", MarketKind::STOCK, s) && std::string(s.sym) == "BTC-USD" &&
             market_yahoo_symbol(s) == "BTC-USD",
         "stock kind keeps the dash");

  // Slots read from flash are checked too.
  MarketSymbol odd = market_symbol("AAPL", MarketKind::STOCK);
  MCHECK(market_symbol_valid(odd), "plain");
  odd.kind = 9;
  MCHECK(!market_symbol_valid(odd), "unknown kind");
  odd.kind = (uint8_t) MarketKind::NONE;
  MCHECK(!market_symbol_valid(odd), "no kind");
  memset(odd.sym, 'A', sizeof odd.sym);
  odd.kind = (uint8_t) MarketKind::STOCK;
  MCHECK(!market_symbol_valid(odd), "no terminator");
  MCHECK(!market_symbol_valid(market_symbol("lower", MarketKind::STOCK)), "lower case");
  MCHECK(!market_symbol_valid(market_symbol("A.B", MarketKind::INDEX)), "index punctuation");
  MCHECK(!market_symbol_valid(MarketSymbol{}), "empty");

  MarketKind k = MarketKind::STOCK;
  MCHECK(market_kind_by_key("crypto", k) && k == MarketKind::CRYPTO, "crypto");
  MCHECK(market_kind_by_key("", k) && k == MarketKind::NONE, "empty is a guess");
  MCHECK(!market_kind_by_key("bond", k), "bond");
  MCHECK_EQ(market_kind_key(MarketKind::INDEX), "index");
}

void test_requests() {
  uint8_t slot = 99;
  MarketSymbol s{};
  MCHECK(parse_market("0,aapl", slot, s) && slot == 0 && std::string(s.sym) == "AAPL", "append");
  MCHECK(parse_market("3,sol,stock", slot, s) && slot == 3 && s.kind == (uint8_t) MarketKind::STOCK, "kind given");
  MCHECK(parse_market("8,SOL,", slot, s) && s.kind == (uint8_t) MarketKind::CRYPTO, "empty kind guesses");
  MCHECK(parse_market("1,^dji,index", slot, s) && s.kind == (uint8_t) MarketKind::INDEX, "index");
  for (const char *bad : {"", "1", "9,AAPL", "a,AAPL", "1,AAPL,bond", "1,AAPL,stock,x", "1,,stock", "11,AAPL",
                          "1,^GSPC,crypto", ",AAPL"})
    MCHECK(!parse_market(bad, slot, s), "\"%s\" refused", bad);
  MCHECK(parse_market_slot("8", slot) && slot == 8, "last slot");
  MCHECK(!parse_market_slot("0", slot), "0 needs free_ok");
  MCHECK(parse_market_slot("0", slot, true) && slot == 0, "0 appends");
  MCHECK(!parse_market_slot("9", slot, true), "past the end");
  uint8_t from = 0, to = 0;
  MCHECK(parse_market_move("3,1", from, to) && from == 3 && to == 1, "move");
  for (const char *bad : {"", "3", "3,", "0,1", "3,9", "3,1,2", "a,b"})
    MCHECK(!parse_market_move(bad, from, to), "\"%s\" refused", bad);
}

std::string order(const MarketBlob &b) {
  std::string out;
  for (uint8_t i = 0; i < market_count(b); i++)
    out += (out.empty() ? "" : " ") + std::string(b.syms[i].sym);
  return out;
}

void test_list() {
  MarketBlob b{};
  MCHECK(market_count(b) == 0 && market_spark_slot(b) == -1, "empty");
  MCHECK(market_put(b, 0, sym("AAPL")) == 1, "first");
  MCHECK(market_put(b, 0, sym("BTC")) == 2, "second");
  MCHECK(market_put(b, 7, sym("SPY")) == 3, "past the end appends");
  MCHECK(market_put(b, 0, sym("aapl")) == 1 && market_count(b) == 3, "again: where it is");
  MCHECK(market_put(b, 0, sym("SOL", MarketKind::STOCK)) == 4, "the stock beside no coin");
  MCHECK(market_put(b, 0, sym("SOL")) == 5, "the coin is another symbol");
  MCHECK(market_put(b, 2, sym("ETH")) == 2, "replaces");
  MCHECK_EQ(order(b), "AAPL ETH SPY SOL SOL");
  MCHECK(market_put(b, 0, MarketSymbol{}) == 0 && market_count(b) == 5, "empty refused");
  for (const char *t : {"MSFT", "NVDA", "QQQ"})
    market_put(b, 0, sym(t));
  MCHECK(market_count(b) == 8, "full");
  MCHECK(market_put(b, 0, sym("TSLA")) == 0, "no room");
  MCHECK(market_put(b, 9, sym("TSLA")) == 0, "no slot 9");

  b.spark = 7;  // NVDA has it
  MCHECK(market_spark_slot(b) == 6, "sparkline slot");
  MCHECK(market_del(b, 2), "delete ETH");
  MCHECK_EQ(order(b), "AAPL SPY SOL SOL MSFT NVDA QQQ");
  MCHECK(b.spark == 6 && market_empty(b.syms[7]), "sparkline follows NVDA");
  MCHECK(!market_del(b, 8) && !market_del(b, 0), "past the end");
  MCHECK(market_move(b, 6, 1), "NVDA to the top");
  MCHECK_EQ(order(b), "NVDA AAPL SPY SOL SOL MSFT QQQ");
  MCHECK(b.spark == 1, "sparkline moved with it");
  MCHECK(market_move(b, 1, 7) && b.spark == 7, "and to the bottom");
  MCHECK_EQ(order(b), "AAPL SPY SOL SOL MSFT QQQ NVDA");
  b.spark = 2;
  MCHECK(market_move(b, 1, 3) && b.spark == 1, "SPY moves up under a move down");
  MCHECK_EQ(order(b), "SPY SOL AAPL SOL MSFT QQQ NVDA");
  MCHECK(market_move(b, 5, 1) && b.spark == 2, "and down under a move up");
  MCHECK(!market_move(b, 1, 8) && !market_move(b, 0, 1), "out of range");
  MCHECK(market_del(b, 2) && b.spark == 0 && market_spark_slot(b) == 0, "deleting it falls back to the first");

  // Flash from newer firmware or a bad write.
  MarketBlob raw{};
  raw.syms[1] = market_symbol("AAPL", MarketKind::STOCK);
  raw.syms[2] = market_symbol("bad!", MarketKind::STOCK);
  raw.syms[3] = market_symbol("BTC", MarketKind::CRYPTO);
  raw.syms[4] = market_symbol("AAPL", MarketKind::STOCK);
  raw.syms[5] = market_symbol("XYZ", (MarketKind) 7);
  raw.syms[7] = market_symbol("ETH", MarketKind::CRYPTO);
  raw.spark = 8;
  raw.spare[1] = 5;
  MarketList list;
  list.blob = raw;
  list.loaded();
  MCHECK_EQ(order(list.blob), "AAPL BTC ETH");
  MCHECK(list.blob.spark == 3 && list.blob.spare[1] == 0 && market_empty(list.blob.syms[3]), "tidied");
  raw.spark = 3;  // the bad one
  list.blob = raw;
  list.loaded();
  MCHECK(list.blob.spark == 0, "its sparkline slot went");
  MCHECK(!list.dirty, "loading is not a change");
  MCHECK(list.put(0, sym("AAPL")) == 1 && !list.dirty, "a repeat changes nothing");
  MCHECK(list.put(0, sym("SPY")) == 4 && list.dirty, "an append does");
  list.dirty = false;
  MCHECK(!list.del(9) && !list.dirty, "a bad delete does not");
  MCHECK(list.move(4, 1) && list.dirty, "a move does");
  list.dirty = false;
  MCHECK(!list.spark(5) && list.spark(2) && list.dirty && list.blob.spark == 2, "sparkline slot");
}

void test_urls() {
  const MarketBlob b = fixture_list();
  MCHECK_EQ(market_spark_url(b), "https://query1.finance.yahoo.com/v7/finance/spark?symbols=AAPL,SPY,%5EGSPC,BRK-B,"
                                 "BTC-USD,ETH-USD,DOGE-USD,SHIB-USD&range=1d&interval=5m");
  MCHECK_EQ(market_kraken_url(b), "https://api.kraken.com/0/public/Ticker?pair=XXBTZUSD,XETHZUSD,XDGUSD,SHIBUSD");
  MCHECK_EQ(market_nasdaq_url(b),
            "https://api.nasdaq.com/api/quote/watchlist?symbol=aapl%7Cstocks&symbol=aapl%7Cetf&symbol=spy%7Cstocks"
            "&symbol=spy%7Cetf&symbol=brk.b%7Cstocks&symbol=brk.b%7Cetf");
  MCHECK_EQ(market_chart_url(sym("GC=F")), "https://query1.finance.yahoo.com/v8/finance/chart/GC%3DF?range=1d&interval=5m");
  MCHECK_EQ(market_chart_url(sym("SPX")), "https://query1.finance.yahoo.com/v8/finance/chart/%5EGSPC?range=1d&interval=5m");
  const MarketBlob none{};
  MCHECK(market_spark_url(none).empty() && market_kraken_url(none).empty() && market_nasdaq_url(none).empty(), "empty");
  MarketBlob odd{};
  market_put(odd, 0, sym("HBAR-USD"));
  market_put(odd, 0, sym("^FTSE"));
  market_put(odd, 0, sym("NDX"));
  MCHECK(market_kraken_url(odd).empty(), "Kraken only gets coins it lists");
  MCHECK_EQ(market_nasdaq_url(odd), "https://api.nasdaq.com/api/quote/watchlist?symbol=ndx%7Cindex");
  MCHECK(is_market_url(market_spark_url(b)) && is_market_url(market_kraken_url(b)) && is_market_url(market_nasdaq_url(b)),
         "ours");
  MCHECK(!is_market_url("https://site.api.espn.com/apis/site/v2/sports"), "ESPN's");
  MCHECK_EQ(kMarketsUserAgent, "Mozilla/5.0");
}

void test_yahoo() {
  const MarketBlob b = fixture_list();
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  MCHECK(parse_yahoo_quotes(json, b, q) == 8, "eight read");
  for (int i = 0; i < kMarketSlots; i++)
    MCHECK(q[i].valid, "slot %d", i);
  // Yahoo lists ETH first; each lands on its slot.
  const MarketQuote &aapl = q[0];
  MCHECK_NEAR(aapl.price, 355.63, 1e-4);
  MCHECK_NEAR(aapl.prev, 348.51, 1e-4);
  MCHECK(aapl.time == 1794430801 && aapl.utc_offset == -18000, "time %u", aapl.time);
  MCHECK(aapl.pre_at == kPre && aapl.open_at == kOpen && aapl.close_at == kClose && aapl.post_at == kPost, "session");
  MCHECK(filled(aapl) == kSparkPoints, "79 bars fill every bucket: %d", filled(aapl));
  MCHECK_NEAR(aapl.spark_hi, 355.63, 1e-3);
  MCHECK(aapl.spark[kSparkPoints - 1] == kSparkTop, "ends on the day's high");
  MCHECK_NEAR(q[2].price, 7619.38, 1e-2);
  MCHECK_NEAR(q[3].price, 588.24, 1e-3);
  const MarketQuote &btc = q[4];
  MCHECK_NEAR(btc.price, 73738.65, 1e-2);
  MCHECK_NEAR(btc.prev, 73925.19, 1e-2);
  MCHECK(btc.open_at == kUtcDay && btc.close_at == 1794527940 && btc.utc_offset == 0, "the UTC day");
  MCHECK(filled(btc) == 11 && btc.spark[10] != kSparkEmpty && btc.spark[11] == kSparkEmpty, "four hours in: %d",
         filled(btc));
  MCHECK_NEAR(q[6].price, 0.06726, 1e-7);
  MCHECK_NEAR(q[7].price, 4.35e-6, 1e-11);
  MCHECK(filled(q[7]) == 10, "SHIB skips quiet bars: %d", filled(q[7]));

  // Unknown symbols are left out of the answer.
  MarketBlob two{};
  market_put(two, 0, sym("ZZZZQ"));
  market_put(two, 0, sym("IXIC"));
  MarketQuote r[kMarketSlots];
  r[0].valid = true;
  r[0].price = 1;
  json = slurp("fixtures/markets_yahoo_spark_unknown.json");
  MCHECK(parse_yahoo_quotes(json, two, r) == 1, "one known");
  MCHECK(r[0].valid && r[0].price == 1 && r[1].valid, "the unknown slot is untouched");
  json = slurp("fixtures/markets_yahoo_spark_none.json");
  MarketQuote none[kMarketSlots];
  MCHECK(parse_yahoo_quotes(json, two, none) == 0 && !none[0].valid, "Yahoo's 404 answer");

  // The one-symbol chart reads the same.
  MarketBlob one{};
  market_put(one, 0, sym("AAPL"));
  MarketQuote c[kMarketSlots];
  json = slurp("fixtures/markets_yahoo_chart_aapl.json");
  MCHECK(parse_yahoo_quotes(json, one, c) == 1 && filled(c[0]) == kSparkPoints, "chart");
  MCHECK_NEAR(c[0].price, 355.63, 1e-4);

  MarketQuote g[kMarketSlots];
  std::string html = "<html>Edge: Too Many Requests</html>";
  MCHECK(parse_yahoo_quotes(html, b, g) == -1, "not JSON");
  json = slurp("fixtures/markets_kraken_ticker.json");
  MCHECK(parse_yahoo_quotes(json, b, g) == -1, "not Yahoo's");
  std::string bare = "{\"spark\":{\"result\":[{\"symbol\":\"AAPL\",\"response\":[{\"meta\":{\"symbol\":\"AAPL\"}}]}]}}";
  MCHECK(parse_yahoo_quotes(bare, b, g) == 0 && !g[0].valid, "no price");
  std::string cut = json.substr(0, json.size() / 2);
  MCHECK(parse_yahoo_quotes(cut, b, g) == -1, "cut short");
}

void test_kraken() {
  MarketBlob b{};
  for (const char *t : {"AAPL", "BTC", "DOGE", "SOL", "LINK"})
    market_put(b, 0, sym(t));
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_kraken_ticker.json");
  MCHECK(parse_kraken_quotes(json, b, kNow, q) == 3, "three of its five");
  MCHECK(!q[0].valid && q[1].valid && q[2].valid && q[3].valid && !q[4].valid, "slots");
  MCHECK_NEAR(q[1].price, 73746.3, 1e-6);
  MCHECK_NEAR(q[1].prev, 73928.5, 1e-6);
  MCHECK_NEAR(q[2].price, 0.0672463, 1e-9);
  MCHECK(q[1].time == kNow && q[1].open_at == kUtcDay && q[1].close_at == kUtcDay + 86400, "the UTC day");
  MCHECK(filled(q[1]) == 0, "no sparkline");
  MCHECK(market_state(q[1], MarketKind::CRYPTO, kNow) == MarketState::ALWAYS, "always");
  MCHECK_EQ(market_percent_text(market_percent(q[1]), 9), "-0.25%");
  MarketQuote r[kMarketSlots];
  json = slurp("fixtures/markets_kraken_unknown.json");
  MCHECK(parse_kraken_quotes(json, b, kNow, r) == -1 && !r[1].valid, "one unknown pair fails the read");
  json = slurp("fixtures/markets_nasdaq_watchlist.json");
  MCHECK(parse_kraken_quotes(json, b, kNow, r) == -1, "not Kraken's");
}

void test_nasdaq() {
  MarketBlob b{};
  for (const char *t : {"AAPL", "SPY", "BRK.B", "BTC"})
    market_put(b, 0, sym(t));
  market_put(b, 0, sym("COMP", MarketKind::INDEX));
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_nasdaq_watchlist.json");
  MCHECK(parse_nasdaq_quotes(json, b, kNow, q) == 4, "four");
  MCHECK(q[0].valid && q[1].valid && q[2].valid && !q[3].valid && q[4].valid, "slots");
  MCHECK_NEAR(q[0].price, 355.63, 1e-9);
  MCHECK_NEAR(q[0].prev, 348.51, 1e-4);
  MCHECK_NEAR(q[2].price, 588.44, 1e-9);
  MCHECK_NEAR(q[4].price, 25466.19, 1e-6);
  MCHECK_NEAR(q[4].prev, 25065.88, 1e-2);
  MCHECK(q[0].time == kNow && q[0].open_at == 0, "no session times");
  MCHECK(market_state(q[0], MarketKind::STOCK, kNow) == MarketState::CLOSED, "Market Closed");
  MCHECK_EQ(market_percent_text(market_percent(q[2]), 9), "+0.87%");
  std::string netonly = "{\"data\":[{\"symbol\":\"AAPL\",\"lastSalePrice\":\"$10.00\",\"netChange\":\"-0.50\"}]}";
  MarketQuote r[kMarketSlots];
  MCHECK(parse_nasdaq_quotes(netonly, b, kNow, r) == 1, "net change alone");
  MCHECK_NEAR(r[0].prev, 10.5, 1e-9);
  std::string empty = "{\"data\":null,\"status\":{\"rCode\":200}}";
  MCHECK(parse_nasdaq_quotes(empty, b, kNow, r) == 0, "nothing found");
  std::string other = "{\"error\":[]}";
  MCHECK(parse_nasdaq_quotes(other, b, kNow, r) == -1, "not Nasdaq's");
  MCHECK(market_state_word("Pre-Market") == MarketState::PRE && market_state_word("After-Hours") == MarketState::POST &&
             market_state_word("Market Open") == MarketState::OPEN &&
             market_state_word("Market Closed") == MarketState::CLOSED && market_state_word("") == MarketState::UNKNOWN,
         "words");
}

void test_state() {
  const MarketBlob b = fixture_list();
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  parse_yahoo_quotes(json, b, q);
  const MarketQuote &a = q[0];
  const MarketKind S = MarketKind::STOCK;
  MCHECK(market_state(a, S, kPre - 1) == MarketState::CLOSED, "before 4 AM");
  MCHECK(market_state(a, S, kPre) == MarketState::PRE, "pre-market");
  MCHECK(market_state(a, S, kOpen) == MarketState::OPEN, "the bell");
  MCHECK(market_state(a, S, kClose - 1) == MarketState::OPEN, "the last second");
  MCHECK(market_state(a, S, kClose) == MarketState::POST, "after hours");
  MCHECK(market_state(a, S, kPost) == MarketState::CLOSED, "8 PM");
  MCHECK(market_state(a, S, kNow) == MarketState::CLOSED, "read at night");
  MCHECK(market_state(q[4], MarketKind::CRYPTO, kNow) == MarketState::ALWAYS, "coins");
  MCHECK(market_state(MarketQuote{}, S, kNow) == MarketState::UNKNOWN, "nothing read");
  MCHECK_EQ(market_state_key(MarketState::POST), "post");

  MCHECK_EQ(market_state_text(MarketState::PRE, a, kPre, true), "PRE-MARKET");
  MCHECK_EQ(market_state_text(MarketState::PRE, a, kPre, false), "PRE");
  MCHECK_EQ(market_state_text(MarketState::POST, a, kClose, true), "AFTER HOURS");
  MCHECK_EQ(market_state_text(MarketState::OPEN, a, kOpen, false), "OPEN");
  // Wednesday's close, 11 PM Eastern that night
  MCHECK_EQ(market_state_text(MarketState::CLOSED, a, kNow, true), "CLOSED");
  // 2 AM Thursday, then a Saturday
  MCHECK_EQ(market_state_text(MarketState::CLOSED, a, kNow + 3 * 3600, true), "WED CLOSE");
  MCHECK_EQ(market_state_text(MarketState::CLOSED, a, kNow + 3 * 86400, false), "WED");
  MCHECK_EQ(market_state_text(MarketState::CLOSED, a, kNow + 9 * 86400, true), "CLOSED");
  MCHECK_EQ(market_state_text(MarketState::ALWAYS, q[4], kNow, true), "");

  MCHECK(!market_stale(q[4], MarketKind::CRYPTO, kNow), "fresh coin");
  MCHECK(market_stale(q[4], MarketKind::CRYPTO, kNow + 31 * 60), "a coin's price half an hour old");
  MCHECK(!market_stale(a, S, kNow + 31 * 60), "closed markets keep their close");
  MCHECK(market_stale(a, S, kOpen + 3600) == false && market_stale(a, S, kOpen + 6 * 3600 + 50 * 60) == false,
         "a trade at the close");
  MarketQuote mid = a;
  mid.time = (uint32_t) (kOpen + 3600);
  MCHECK(market_stale(mid, S, kOpen + 3600 + 31 * 60), "open, but no trade for 31 minutes");

  MCHECK(market_poll_ms(b, q, kNow) == kMarketPollMs, "coins every minute");
  MarketBlob stocks{};
  for (const char *t : {"AAPL", "SPY"})
    market_put(stocks, 0, sym(t));
  MCHECK(market_poll_ms(stocks, q, kNow) == kMarketClosedPollMs, "closed stocks rest");
  MCHECK(market_poll_ms(stocks, q, kOpen + 60) == kMarketPollMs, "open");
  MCHECK(market_poll_ms(stocks, q, kPre + 60) == kMarketPollMs, "pre-market");
  MarketQuote unread[kMarketSlots];
  MCHECK(market_poll_ms(stocks, unread, kNow) == kMarketPollMs, "nothing read yet");

  MarketQuote mixed[kMarketSlots];
  mixed[0] = q[0];
  mixed[1] = q[0];
  mixed[1].state = (uint8_t) MarketState::OPEN;
  mixed[1].open_at = 0;
  MarketBoardState st = market_board_state(stocks, mixed, kNow);
  MCHECK(st.state == MarketState::OPEN && st.slot == 1, "open wins");
  st = market_board_state(stocks, q, kNow);
  MCHECK(st.state == MarketState::CLOSED && st.slot == 0, "closed");
  MarketBlob coins{};
  market_put(coins, 0, sym("BTC"));
  MCHECK(market_board_state(coins, q, kNow).slot == -1, "coins only");
}

void test_text() {
  MCHECK(market_decimals(355.63) == 2 && market_decimals(1) == 2 && market_decimals(0) == 2, "2 from 1 up");
  MCHECK(market_decimals(0.1234) == 4 && market_decimals(0.06726) == 5 && market_decimals(4.35e-6) == 8 &&
             market_decimals(-0.5) == 4,
         "4 significant below 1");
  struct P {
    double v;
    size_t max;
    const char *out;
  };
  const P prices[] = {
      {355.63, 10, "355.63"},       {73738.65, 10, "73,738.65"}, {73738.65, 8, "73738.65"}, {73738.65, 6, "73,739"},
      {104321.4, 7, "104,321"},     {104321.4, 6, "104321"},     {104321.4, 5, "104K"},     {1234.56, 8, "1,234.56"},
      {1234.56, 7, "1234.56"},      {1234.56, 5, "1,235"},       {0.1234, 6, "0.1234"},     {0.06726, 10, "0.06726"},
      {0.06726, 6, "0.0673"},       {0.06726, 5, "0.067"},       {4.35e-6, 10, "0.00000435"},
      {4.35e-6, 8, "0.000004"},     {0, 6, "0.00"},              {-37.63, 8, "-37.63"},     {1234567.8, 5, "1.2M"},
      {52486.49, 4, "52486"},       {999.999, 10, "1,000.00"},   {NAN, 6, "--"},
  };
  for (const P &p : prices)
    MCHECK_EQ(market_price_text(p.v, p.max), p.out);
  MCHECK_EQ(market_change_text(7.12, 355.63, 10), "+7.12");
  MCHECK_EQ(market_change_text(-186.54, 73738.65, 10), "-186.54");
  MCHECK_EQ(market_change_text(-186.54, 73738.65, 4), "-187");
  MCHECK_EQ(market_change_text(1234.5, 52486.49, 9), "+1,234.50");
  MCHECK_EQ(market_change_text(-0.00024604, 0.06726, 10), "-0.00025");
  MCHECK_EQ(market_change_text(-0.00024604, 0.06726, 6), "-0.0002");
  MCHECK_EQ(market_change_text(-0.00024604, 0.06726, 4), "-0.0002");
  MCHECK_EQ(market_change_text(0.000001, 0.06726, 10), "0.00000");
  MCHECK_EQ(market_change_text(0, 355.63, 10), "0.00");
  MCHECK_EQ(market_change_text(-0.004, 355.63, 10), "0.00");
  MCHECK_EQ(market_percent_text(2.043, 9), "+2.04%");
  MCHECK_EQ(market_percent_text(2.043, 5), "+2.0%");
  MCHECK_EQ(market_percent_text(2.043, 3), "+2%");
  MCHECK_EQ(market_percent_text(-12.345, 9), "-12.3%");
  MCHECK_EQ(market_percent_text(250, 9), "+250%");
  MCHECK_EQ(market_percent_text(-0.004, 9), "0.00%");
  MCHECK_EQ(market_percent_text(NAN, 9), "");

  MarketQuote q;
  q.valid = true;
  q.price = 355.63;
  q.prev = 355.634;
  MCHECK(market_dir(q) == 0 && market_color(market_dir(q)) == kMarketFlat, "rounds to flat");
  q.prev = 355.62;
  MCHECK(market_dir(q) == 1 && market_color(1) == kBetGood, "up");
  q.prev = 0;
  MCHECK(market_dir(q) == 0 && std::isnan(market_percent(q)) && market_change(q) == 0, "no close known");
  MCHECK(market_color(-1) == kBetBad && kMarketUp == 0x2ED573 && kMarketDown == 0xFF3B3B, "the bet colors");
  // The panel's fonts: ASCII and Latin-1 only.
  for (double v : {355.63, -0.00024604, 1e300, -1e300, 4.35e-6})
    for (size_t max : {4, 8, 12})
      for (const std::string &s : {market_price_text(v, max), market_change_text(v, 1, max), market_percent_text(v, max)})
        for (char ch : s)
          MCHECK(ch >= 0x20 && ch < 0x7F, "printable in \"%s\"", s.c_str());
}

void test_spark() {
  MCHECK(market_spark_bucket(kOpen, kOpen, kClose) == 0, "the bell");
  MCHECK(market_spark_bucket(kOpen - 1, kOpen, kClose) == -1, "before");
  MCHECK(market_spark_bucket(kClose, kOpen, kClose) == kSparkPoints - 1, "the closing bar");
  MCHECK(market_spark_bucket(kClose + 600, kOpen, kClose) == kSparkPoints - 1, "after");
  MCHECK(market_spark_bucket((kOpen + kClose) / 2, kOpen, kClose) == kSparkPoints / 2, "midday");
  MCHECK(market_spark_bucket(kOpen, kOpen, kOpen) == -1, "no session");

  float v[kSparkPoints];
  for (float &x : v)
    x = NAN;
  MarketQuote q;
  q.valid = true;
  market_spark_pack(q, v);
  MCHECK(filled(q) == 0 && q.spark_lo == 0 && q.spark_hi == 0, "nothing");
  v[0] = 10;
  v[5] = 20;
  v[9] = 15;
  market_spark_pack(q, v);
  MCHECK(q.spark[0] == 0 && q.spark[5] == kSparkTop && q.spark[9] == 127 && filled(q) == 3, "scaled");
  MCHECK_NEAR(market_spark_value(q, 9), 15, 0.05);
  v[5] = v[9] = 10;
  market_spark_pack(q, v);
  MCHECK(q.spark[0] == kSparkTop / 2 && q.spark[9] == kSparkTop / 2, "flat");

  const MarketBlob b = fixture_list();
  MarketQuote r[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  parse_yahoo_quotes(json, b, r);
  const SparkLine a = market_spark(r[0], 3, 40, 64, 15);
  MCHECK(a.n == kSparkPoints && a.pts[0].x == 3 && a.pts[a.n - 1].x == 3 + 63, "AAPL spans the box");
  bool inside = true;
  for (int i = 0; i < a.n; i++)
    inside = inside && a.pts[i].y >= 40 && a.pts[i].y < 55 && (i == 0 || a.pts[i].x >= a.pts[i - 1].x);
  MCHECK(inside, "rows in the box, left to right");
  MCHECK(a.base_y == 54 && a.color == kMarketUp, "gapped up from the close at the bottom");
  MCHECK(a.pts[a.n - 1].y < a.pts[0].y, "closed above its open");
  const SparkLine btc = market_spark(r[4], 0, 0, 128, 20);
  MCHECK(btc.n == 11 && btc.pts[10].x <= 128 / 5 && btc.color == kMarketDown, "a sixth of the UTC day");
  MCHECK(btc.base_y >= 0 && btc.base_y < 20, "the open within");
  const SparkLine wide = market_spark(r[4], 0, 0, 128, 20, true);
  MCHECK(wide.n == 11 && wide.pts[0].x == 0 && wide.pts[10].x == 127, "stretched");
  MarketQuote one = r[0];
  memset(one.spark, kSparkEmpty, sizeof one.spark);
  one.spark[3] = 10;
  MCHECK(market_spark(one, 0, 0, 64, 15).n == 0, "one point draws nothing");
  MCHECK(market_spark(MarketQuote{}, 0, 0, 64, 15).n == 0, "nothing read");
  MCHECK(market_spark(r[0], 0, 0, 1, 15).n == 0, "no room");
}

void test_frames() {
  for (int w : {64, 128, 192, 256})
    for (bool ticker : {true, false})
      for (bool spark : {true, false}) {
        const MarketGeom g = market_geom(w, 64, ticker, spark);
        const int bottom = 64 - (ticker ? 8 : 0);
        MCHECK(g.row_y + g.rows * kMarketRowPx <= bottom && g.rows >= 4, "w%d rows %d", w, g.rows);
        MCHECK(g.cols == (w < 128 ? 2 : w < 192 ? 3 : 4), "w%d cols", w);
        int right = 0;
        for (const MarketCol *c : {&g.sym, &g.price, &g.change, &g.pct}) {
          if (c->w == 0)
            continue;
          MCHECK(c->x >= right && c->x + c->w <= w, "w%d columns in order", w);
          right = c->x + c->w;
        }
        MCHECK(g.sym.chars() >= 6 && g.price.chars() >= 6, "w%d room", w);
        MCHECK(g.spark == spark, "w%d sparkline", w);
        if (!g.spark)
          continue;
        MCHECK(g.spark_x >= 0 && g.spark_x + g.spark_w <= w && g.spark_y + g.spark_h <= bottom && g.spark_h >= 14,
               "w%d sparkline box", w);
        const bool beside = g.spark_x >= right;
        MCHECK(beside || g.spark_y > g.row_y + g.rows * kMarketRowPx - 1, "w%d sparkline clear of the rows", w);
        MCHECK(beside == (w >= 256), "w%d beside only on four panels", w);
      }

  const MarketBlob b = fixture_list();
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  parse_yahoo_quotes(json, b, q);

  MarketGeom g = market_geom(128, 64, true, false);
  MarketFrame f = market_frame(b, q, g, kNow, 0, false);
  MCHECK(f.n == 6 && f.pages == 2 && f.page == 0, "first page");
  MCHECK_EQ(f.title, "MARKETS 1/2");
  MCHECK_EQ(f.status, "CLOSED");
  MCHECK(f.status_color == kMarketShutColor, "gray");
  MCHECK_EQ(f.rows[0].sym, "AAPL");
  MCHECK_EQ(f.rows[0].price, "355.63");
  MCHECK_EQ(f.rows[0].pct, "+2.04%");
  MCHECK(f.rows[0].change.empty() && f.rows[0].color == kMarketUp, "three columns");
  MCHECK_EQ(f.rows[4].price, "73,738.65");
  MCHECK_EQ(f.rows[4].pct, "-0.25%");
  MCHECK(f.rows[4].color == kMarketDown, "BTC down");
  for (int i = 0; i < f.n; i++)
    MCHECK(f.rows[i].sym.size() <= g.sym.chars() && f.rows[i].price.size() <= g.price.chars() &&
               f.rows[i].pct.size() <= g.pct.chars(),
           "row %d fits", i);
  f = market_frame(b, q, g, kNow, 3, false);
  MCHECK(f.n == 2 && f.page == 1, "the page wraps");
  MCHECK_EQ(f.title, "MARKETS 2/2");
  MCHECK_EQ(f.rows[0].sym, "DOGE");
  MCHECK_EQ(f.rows[0].price, "0.06726");
  MCHECK_EQ(f.rows[1].price, "0.00000435");

  g = market_geom(256, 64, true, true);
  f = market_frame(b, q, g, kOpen + 3600, 0, false);
  MCHECK(f.n == 6 && f.spark && f.rows[0].spark && !f.rows[1].spark, "the first symbol's line");
  MCHECK_EQ(f.status, "OPEN");
  MCHECK(f.status_color == kMarketOpenColor, "green");
  MCHECK_EQ(f.rows[2].change, "+98.45");
  MCHECK_EQ(f.rows[4].change, "-186.54");
  MCHECK(f.line.pts[0].x == g.spark_x && f.line.pts[f.line.n - 1].x == g.spark_x + g.spark_w - 1, "in its box");

  g = market_geom(64, 64, true, true);
  MarketBlob pick = b;
  pick.spark = 5;
  f = market_frame(pick, q, g, kNow, 1, false);
  MCHECK(f.n == 4 && f.title.empty(), "one panel: no title");
  MCHECK_EQ(f.status, "CLOSED");
  MCHECK_EQ(f.rows[0].sym, "BTC");
  MCHECK_EQ(f.rows[0].price, "73,739");
  MCHECK(f.rows[0].spark && f.spark && f.line.n == 11, "the picked symbol's line");
  f = market_frame(pick, q, g, kNow, 1, true);
  MCHECK_EQ(f.rows[0].price, "-0.25%");

  MarketBlob coins{};
  for (const char *t : {"BTC", "ETH"})
    market_put(coins, 0, sym(t));
  MarketQuote cq[kMarketSlots];
  cq[0] = q[4];
  f = market_frame(coins, cq, market_geom(64, 64, true, false), kNow + 40 * 60, 0, false);
  MCHECK_EQ(f.status, "CRYPTO");
  MCHECK(f.rows[0].color == kMarketStale, "an old price is gray");
  MCHECK_EQ(f.rows[1].price, "--");
  f = market_frame(coins, cq, market_geom(128, 64, true, false), kNow, 0, false);
  MCHECK_EQ(f.title, "CRYPTO");
  MCHECK(f.status.empty() && f.pages == 1, "no market hours");
  f = market_frame(MarketBlob{}, cq, market_geom(128, 64, true, false), kNow, 0, false);
  MCHECK(f.n == 0 && f.pages == 1, "nothing listed");
}

void test_card() {
  const MarketBlob b = fixture_list();
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  parse_yahoo_quotes(json, b, q);
  using esphome::gameday::font_px;
  using esphome::gameday::font_rows;
  for (int i = 0; i < kMarketSlots; i++)
    for (bool ticker : {true, false}) {
      const MarketCard c = market_card(b.syms[i], q[i], 64, 64, ticker, kNow);
      const int bottom = 64 - (ticker ? 8 : 0);
      MCHECK((int) c.sym.size() * font_px(c.sym_font) + (c.status.empty() ? 0 : 2 + (int) c.status.size() * 5) <= 64,
             "%s top line fits", c.sym.c_str());
      MCHECK((int) c.price.size() * font_px(c.price_font) <= 64, "%s price fits", c.sym.c_str());
      MCHECK(c.change.size() + (c.change.empty() ? 0 : 1) + c.pct.size() <= 12, "%s move fits", c.sym.c_str());
      MCHECK(c.price_at.y >= font_rows(c.sym_font) && c.change_at.y >= c.price_at.y + font_rows(c.price_font),
             "%s rows apart", c.sym.c_str());
      MCHECK(c.spark && c.line.n >= 2, "%s line", c.sym.c_str());
      for (int k = 0; k < c.line.n; k++)
        MCHECK(c.line.pts[k].y > c.change_at.y + 8 && c.line.pts[k].y < bottom && c.line.pts[k].x < 64,
               "%s line in its box", c.sym.c_str());
    }
  MarketCard c = market_card(b.syms[0], q[0], 64, 64, true, kNow);
  MCHECK_EQ(c.status, "CLOSED");
  MCHECK_EQ(c.price, "355.63");
  MCHECK_EQ(c.change, "+7.12");
  MCHECK_EQ(c.pct, "+2.04%");
  MCHECK(c.price_font == MarketFont::S16 && c.sym_font == MarketFont::S12 && c.color == kMarketUp, "fonts");
  c = market_card(b.syms[0], q[0], 64, 64, true, kOpen + 60);
  MCHECK(c.status.empty(), "open says nothing");
  c = market_card(b.syms[2], q[2], 64, 64, true, kNow);
  MCHECK_EQ(c.price, "7,619.38");
  MCHECK_EQ(c.change, "+98");
  c = market_card(b.syms[4], q[4], 64, 64, true, kNow);
  MCHECK_EQ(c.price, "73738.65");
  MCHECK(c.status.empty() && c.color == kMarketDown, "coins have no hours");
  c = market_card(b.syms[7], q[7], 64, 64, true, kNow);
  MCHECK_EQ(c.price, "0.00000435");
  MCHECK(c.price_font == MarketFont::S12, "every decimal in spleen_12");
  c = market_card(sym("NASDAQ"), q[2], 64, 64, true, kNow);
  MCHECK(c.sym_font == MarketFont::S8 && c.status == "CLOSED", "a long symbol drops a size");
  c = market_card(sym("EURUSD=X"), q[0], 64, 64, true, kNow);
  MCHECK(c.sym_font == MarketFont::S12 && c.status.empty(), "then the status goes");
  c = market_card(b.syms[0], MarketQuote{}, 64, 64, true, kNow);
  MCHECK(c.price == "--" && c.pct.empty() && !c.spark, "nothing read");
}

void test_ticker() {
  const MarketBlob b = fixture_list();
  MarketQuote q[kMarketSlots];
  std::string json = slurp("fixtures/markets_yahoo_spark.json");
  parse_yahoo_quotes(json, b, q);
  std::vector<esphome::gameday::TickerSpan> spans;
  const std::string line = market_ticker(b, q, kNow, &spans);
  MCHECK_EQ(line,
            "AAPL 355.63 +7.12 +2.04% | SPY 759.81 +9.89 +1.32% | GSPC 7,619.38 +98.45 +1.31% | BRK.B 588.24 +4.90 "
            "+0.84% | BTC 73,738.65 -186.54 -0.25% | ETH 2,052.44 -1.17 -0.06% | DOGE 0.06726 -0.00025 -0.36% | SHIB "
            "0.00000435 -0.00000001 -0.30%");
  MCHECK(spans.size() == 8 && spans[0].rgb == kMarketUp && spans[4].rgb == kMarketDown, "every move colored");
  const std::string marked = esphome::gameday::ticker_markup(line, spans);
  MCHECK(marked.find("AAPL 355.63 `2ED573 +7.12 +2.04%` | SPY") == 0, "%s", marked.c_str());
  MCHECK(marked.find("BTC 73,738.65 `FF3B3B -186.54 -0.25%`") != std::string::npos, "BTC red");
  MCHECK(esphome::gameday::panel_text(line) == line, "panel text already");
  MarketQuote partial[kMarketSlots];
  partial[1] = q[1];
  partial[1].prev = 0;
  spans.clear();
  MCHECK_EQ(market_ticker(b, partial, kNow, &spans), "SPY 759.81");
  MCHECK(spans.empty(), "no move, no color");
  MCHECK(market_ticker(b, q, kNow, nullptr) == line, "spans optional");
}

}  // namespace

int run_market_tests(int &total_checks) {
  test_symbols();
  test_requests();
  test_list();
  test_urls();
  test_yahoo();
  test_kraken();
  test_nasdaq();
  test_state();
  test_text();
  test_spark();
  test_frames();
  test_card();
  test_ticker();
  total_checks += checks;
  return failures;
}
