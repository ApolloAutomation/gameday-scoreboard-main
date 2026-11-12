// Stocks & Crypto: symbols, quotes, sources and text.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "bets.h"

namespace espn {

// Blob size: changing it loses saved symbols.
constexpr int kMarketSlots = 8;
constexpr int kMarketSymMax = 10;
constexpr int kSparkPoints = 64;
constexpr uint8_t kSparkEmpty = 0xFF;
constexpr uint8_t kSparkTop = 254;

enum class MarketKind : uint8_t { NONE = 0, STOCK = 1, INDEX = 2, CRYPTO = 3 };
// ALWAYS: crypto trades around the clock.
enum class MarketState : uint8_t { UNKNOWN = 0, PRE, OPEN, POST, CLOSED, ALWAYS };

// Saved blob layout: fixed size, no padding.
struct MarketSymbol {
  char sym[kMarketSymMax + 1];  // upper case, zero padded
  uint8_t kind;                 // a MarketKind value
};
static_assert(sizeof(MarketSymbol) == 12, "MarketSymbol is the saved blob's layout");

struct MarketBlob {
  MarketSymbol syms[kMarketSlots];
  uint8_t spark;  // sparkline's slot, 1 based; 0 first
  uint8_t spare[3];
};
static_assert(sizeof(MarketBlob) == 100, "MarketBlob is the saved blob's layout");

// ---- names each source knows ----------------------------------------------------

// Yahoo numbers some coins; Kraken's own pair names.
struct MarketCoin {
  const char *sym, *yahoo, *kraken;
};
static const MarketCoin kMarketCoins[] = {
    {"BTC", "BTC-USD", "XXBTZUSD"},    {"ETH", "ETH-USD", "XETHZUSD"},    {"SOL", "SOL-USD", "SOLUSD"},
    {"DOGE", "DOGE-USD", "XDGUSD"},    {"XRP", "XRP-USD", "XXRPZUSD"},    {"ADA", "ADA-USD", "ADAUSD"},
    {"LTC", "LTC-USD", "XLTCZUSD"},    {"DOT", "DOT-USD", "DOTUSD"},      {"LINK", "LINK-USD", "LINKUSD"},
    {"AVAX", "AVAX-USD", "AVAXUSD"},   {"SHIB", "SHIB-USD", "SHIBUSD"},   {"XLM", "XLM-USD", "XXLMZUSD"},
    {"BCH", "BCH-USD", "BCHUSD"},      {"ATOM", "ATOM-USD", "ATOMUSD"},   {"TRX", "TRX-USD", "TRXUSD"},
    {"UNI", "UNI7083-USD", "UNIUSD"},  {"SUI", "SUI20947-USD", "SUIUSD"}, {"PEPE", "PEPE24478-USD", "PEPEUSD"},
    {"POL", "POL28321-USD", "POLUSD"}, {"TON", "TON11419-USD", "TONUSD"},
};

// guess: typed alone it means the index.
struct MarketIndex {
  const char *sym, *yahoo, *nasdaq;
  bool guess;
};
static const MarketIndex kMarketIndexes[] = {
    {"SPX", "^GSPC", "", true},         {"GSPC", "^GSPC", "", true},       {"DJI", "^DJI", "", true},
    {"DJIA", "^DJI", "", true},         {"DOW", "^DJI", "", false},        {"IXIC", "^IXIC", "COMP", true},
    {"NASDAQ", "^IXIC", "COMP", true},  {"COMP", "^IXIC", "COMP", false},  {"NDX", "^NDX", "NDX", true},
    {"RUT", "^RUT", "", true},          {"VIX", "^VIX", "", true},
};

inline const MarketCoin *market_coin(const char *sym) {
  for (const MarketCoin &c : kMarketCoins)
    if (strcmp(c.sym, sym) == 0)
      return &c;
  return nullptr;
}

inline const MarketIndex *market_index(const char *sym) {
  for (const MarketIndex &i : kMarketIndexes)
    if (strcmp(i.sym, sym) == 0)
      return &i;
  return nullptr;
}

// Other names owners type for a coin.
inline const char *market_coin_alias(const std::string &s) {
  if (s == "XBT")
    return "BTC";
  if (s == "XDG")
    return "DOGE";
  if (s == "MATIC")
    return "POL";
  return nullptr;
}

// ---- symbols and their checks ---------------------------------------------------

inline bool market_alnum(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

inline bool market_alnum_ok(const char *s) {
  const size_t n = strlen(s);
  if (n == 0 || n > (size_t) kMarketSymMax)
    return false;
  for (size_t i = 0; i < n; i++)
    if (!market_alnum(s[i]))
      return false;
  return true;
}

// "BRK.B", "SHOP.TO", "GC=F": no punctuation at either end.
inline bool market_stock_ok(const char *s) {
  const size_t n = strlen(s);
  if (n == 0 || n > (size_t) kMarketSymMax || !market_alnum(s[0]) || !market_alnum(s[n - 1]))
    return false;
  for (size_t i = 1; i < n; i++) {
    const bool punct = s[i] == '.' || s[i] == '-' || s[i] == '=';
    if ((!punct && !market_alnum(s[i])) || (punct && !market_alnum(s[i - 1])))
      return false;
  }
  return true;
}

inline bool market_empty(const MarketSymbol &s) { return s.sym[0] == 0; }

// Flash slots failing this are emptied.
inline bool market_symbol_valid(const MarketSymbol &s) {
  if (s.sym[kMarketSymMax] != 0)
    return false;
  switch ((MarketKind) s.kind) {
    case MarketKind::STOCK:
      return market_stock_ok(s.sym);
    case MarketKind::INDEX:
    case MarketKind::CRYPTO:
      return market_alnum_ok(s.sym);
    default:
      return false;
  }
}

inline bool market_same(const MarketSymbol &a, const MarketSymbol &b) {
  return a.kind == b.kind && strncmp(a.sym, b.sym, sizeof a.sym) == 0;
}

inline MarketSymbol market_symbol(const std::string &sym, MarketKind kind) {
  MarketSymbol s{};
  memcpy(s.sym, sym.data(), sym.size() < (size_t) kMarketSymMax ? sym.size() : (size_t) kMarketSymMax);
  s.kind = (uint8_t) kind;
  return s;
}

// Wire words shared with the page and state document.
inline const char *market_kind_key(MarketKind k) {
  switch (k) {
    case MarketKind::STOCK:
      return "stock";
    case MarketKind::INDEX:
      return "index";
    case MarketKind::CRYPTO:
      return "crypto";
    default:
      return "";
  }
}

inline bool market_kind_by_key(const std::string &key, MarketKind &out) {
  for (MarketKind k : {MarketKind::NONE, MarketKind::STOCK, MarketKind::INDEX, MarketKind::CRYPTO})
    if (key == market_kind_key(k)) {
      out = k;
      return true;
    }
  return false;
}

// "aapl", "^GSPC", "BTC-USD", "BRK/B"; NONE guesses the kind.
inline bool parse_market_symbol(const std::string &text, MarketKind kind, MarketSymbol &out) {
  std::string s;
  for (char c : text) {
    if (c == ' ')
      continue;
    s += c >= 'a' && c <= 'z' ? (char) (c - 'a' + 'A') : c;
    if (s.size() > 16)
      return false;
  }
  if (!s.empty() && s[0] == '$')
    s.erase(0, 1);
  if (!s.empty() && s[0] == '^') {
    if (kind != MarketKind::NONE && kind != MarketKind::INDEX)
      return false;
    s.erase(0, 1);
    kind = MarketKind::INDEX;
  }
  const bool coin_ok = kind == MarketKind::NONE || kind == MarketKind::CRYPTO;
  if (coin_ok && s.size() > 4 && (s.compare(s.size() - 4, 4, "-USD") == 0 || s.compare(s.size() - 4, 4, "/USD") == 0)) {
    s.resize(s.size() - 4);
    kind = MarketKind::CRYPTO;
  }
  if (coin_ok) {
    if (const char *a = market_coin_alias(s))
      s = a;
    if (kind == MarketKind::NONE && market_coin(s.c_str()) != nullptr)
      kind = MarketKind::CRYPTO;
  }
  if (kind == MarketKind::NONE) {
    const MarketIndex *ix = market_index(s.c_str());
    kind = ix != nullptr && ix->guess ? MarketKind::INDEX : MarketKind::STOCK;
  }
  if (s.empty() || s.size() > (size_t) kMarketSymMax)
    return false;
  if (kind == MarketKind::STOCK) {
    // Class shares "BRK/B" and "BRK-B" save as "BRK.B".
    for (char &c : s)
      c = c == '/' ? '.' : c;
    const size_t n = s.size();
    if (n >= 3 && s[n - 2] == '-' && s[n - 1] >= 'A' && s[n - 1] <= 'Z')
      s[n - 2] = '.';
  }
  MarketSymbol m = market_symbol(s, kind);
  if (!market_symbol_valid(m))
    return false;
  out = m;
  return true;
}

// With free_ok, 0 means append.
inline bool parse_market_slot(const std::string &v, uint8_t &slot, bool free_ok = false) {
  if (v.size() != 1 || v[0] < (free_ok ? '0' : '1') || v[0] > '0' + kMarketSlots)
    return false;
  slot = (uint8_t) (v[0] - '0');
  return true;
}

// "0,AAPL", "2,SOL,stock": slot, symbol, optional kind.
inline bool parse_market(const std::string &v, uint8_t &slot, MarketSymbol &out) {
  std::string f[3];
  size_t n = 0, start = 0;
  while (true) {
    if (n == 3)
      return false;
    const size_t comma = v.find(',', start);
    f[n++] = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  MarketKind kind = MarketKind::NONE;
  if (n < 2 || !parse_market_slot(f[0], slot, true) || (n == 3 && !market_kind_by_key(f[2], kind)))
    return false;
  return parse_market_symbol(f[1], kind, out);
}

// "3,1": slot 3 moves to the top.
inline bool parse_market_move(const std::string &v, uint8_t &from, uint8_t &to) {
  const size_t comma = v.find(',');
  return comma != std::string::npos && parse_market_slot(v.substr(0, comma), from) &&
         parse_market_slot(v.substr(comma + 1), to);
}

// ---- the saved list -------------------------------------------------------------

// Slots stay packed: the first empty ends the list.
inline uint8_t market_count(const MarketBlob &b) {
  uint8_t n = 0;
  while (n < kMarketSlots && !market_empty(b.syms[n]))
    n++;
  return n;
}

// 1 based slot, 0 when missing.
inline uint8_t market_find(const MarketBlob &b, const MarketSymbol &s) {
  for (uint8_t i = 0; i < market_count(b); i++)
    if (market_same(b.syms[i], s))
      return (uint8_t) (i + 1);
  return 0;
}

// Slot 0 or past the end appends; 0 when full.
inline uint8_t market_put(MarketBlob &b, uint8_t slot, const MarketSymbol &s) {
  if (slot > kMarketSlots || !market_symbol_valid(s))
    return 0;
  if (const uint8_t at = market_find(b, s))
    return at;
  const uint8_t n = market_count(b);
  if (slot == 0 || slot > n) {
    if (n == kMarketSlots)
      return 0;
    slot = (uint8_t) (n + 1);
  }
  b.syms[slot - 1] = s;
  return slot;
}

// Later slots move up; the sparkline follows its symbol.
inline bool market_del(MarketBlob &b, uint8_t slot) {
  const uint8_t n = market_count(b);
  if (slot == 0 || slot > n)
    return false;
  for (uint8_t i = slot; i < n; i++)
    b.syms[i - 1] = b.syms[i];
  b.syms[n - 1] = MarketSymbol{};
  if (b.spark == slot)
    b.spark = 0;
  else if (b.spark > slot)
    b.spark--;
  return true;
}

inline bool market_move(MarketBlob &b, uint8_t from, uint8_t to) {
  const uint8_t n = market_count(b);
  if (from == 0 || to == 0 || from > n || to > n)
    return false;
  const MarketSymbol s = b.syms[from - 1];
  const int step = to > from ? 1 : -1;
  for (int i = from; i != to; i += step)
    b.syms[i - 1] = b.syms[i - 1 + step];
  b.syms[to - 1] = s;
  if (b.spark == from)
    b.spark = to;
  else if (step > 0 && b.spark > from && b.spark <= to)
    b.spark--;
  else if (step < 0 && b.spark >= to && b.spark < from)
    b.spark++;
  return true;
}

// After loading: bad or repeated slots go; the rest close.
inline void market_tidy(MarketBlob &b) {
  MarketBlob out{};
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMarketSlots; i++) {
    const MarketSymbol &s = b.syms[i];
    if (market_empty(s) || !market_symbol_valid(s) || market_find(out, s) != 0)
      continue;
    out.syms[n++] = s;
    if (b.spark == i + 1)
      out.spark = n;
  }
  b = out;
}

// The sparkline's symbol, 0 based; -1 none.
inline int market_spark_slot(const MarketBlob &b) {
  const uint8_t n = market_count(b);
  if (n == 0)
    return -1;
  return b.spark >= 1 && b.spark <= n ? b.spark - 1 : 0;
}

// Saved as one blob; dirty until saved.
struct MarketList {
  MarketBlob blob{};
  bool dirty{false};
  void loaded() { market_tidy(this->blob); }
  uint8_t put(uint8_t slot, const MarketSymbol &s) {
    const MarketBlob before = this->blob;
    const uint8_t at = market_put(this->blob, slot, s);
    this->dirty |= memcmp(&before, &this->blob, sizeof before) != 0;
    return at;
  }
  bool del(uint8_t slot) { return this->changed(market_del(this->blob, slot)); }
  bool move(uint8_t from, uint8_t to) { return this->changed(market_move(this->blob, from, to)); }
  // 0 shows the first symbol's.
  bool spark(uint8_t slot) {
    if (slot > market_count(this->blob))
      return false;
    this->blob.spark = slot;
    return this->changed(true);
  }

 private:
  bool changed(bool ok) {
    this->dirty |= ok;
    return ok;
  }
};

// ---- the keyless sources --------------------------------------------------------

// Yahoo answers curl with 429; Nasdaq never answers it.
static constexpr const char *kMarketsUserAgent = "Mozilla/5.0";
static constexpr const char *kYahooSparkUrl = "https://query1.finance.yahoo.com/v7/finance/spark?symbols=";
static constexpr const char *kYahooChartUrl = "https://query1.finance.yahoo.com/v8/finance/chart/";
static constexpr const char *kYahooRange = "range=1d&interval=5m";
static constexpr const char *kKrakenUrl = "https://api.kraken.com/0/public/Ticker?pair=";
static constexpr const char *kNasdaqUrl = "https://api.nasdaq.com/api/quote/watchlist?";

inline bool is_market_url(const std::string &url) {
  for (const char *root : {"https://query1.finance.yahoo.com/", "https://api.kraken.com/", "https://api.nasdaq.com/"})
    if (url.compare(0, strlen(root), root) == 0)
      return true;
  return false;
}

inline std::string market_yahoo_symbol(const MarketSymbol &s) {
  std::string sym(s.sym);
  switch ((MarketKind) s.kind) {
    case MarketKind::CRYPTO: {
      const MarketCoin *c = market_coin(s.sym);
      return c != nullptr ? std::string(c->yahoo) : sym + "-USD";
    }
    case MarketKind::INDEX: {
      const MarketIndex *ix = market_index(s.sym);
      return ix != nullptr ? std::string(ix->yahoo) : "^" + sym;
    }
    case MarketKind::STOCK: {
      // Yahoo writes class shares "BRK-B".
      const size_t dot = sym.find('.');
      if (dot != std::string::npos && dot + 2 == sym.size())
        sym[dot] = '-';
      return sym;
    }
    default:
      return "";
  }
}

// Kraken refuses a whole request over one unknown pair.
inline const char *market_kraken_pair(const MarketSymbol &s) {
  const MarketCoin *c = s.kind == (uint8_t) MarketKind::CRYPTO ? market_coin(s.sym) : nullptr;
  return c != nullptr ? c->kraken : "";
}

// US listings only: "brk.b", "comp"; "" none.
inline std::string market_nasdaq_symbol(const MarketSymbol &s) {
  std::string out;
  if (s.kind == (uint8_t) MarketKind::INDEX) {
    const MarketIndex *ix = market_index(s.sym);
    out = ix != nullptr ? ix->nasdaq : "";
  } else if (s.kind == (uint8_t) MarketKind::STOCK) {
    const std::string sym(s.sym);
    const size_t dot = sym.find('.');
    if (sym.find_first_of("=-") == std::string::npos && (dot == std::string::npos || dot + 2 == sym.size()))
      out = sym;
  }
  for (char &c : out)
    c = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
  return out;
}

inline void market_url_append(std::string &url, const std::string &sym) {
  for (char c : sym) {
    if (c == '^')
      url += "%5E";
    else if (c == '=')
      url += "%3D";
    else
      url += c;
  }
}

// Every symbol in one read; "" for an empty list.
inline std::string market_spark_url(const MarketBlob &b) {
  std::string url = kYahooSparkUrl;
  const size_t base = url.size();
  for (uint8_t i = 0; i < market_count(b); i++) {
    if (url.size() > base)
      url += ',';
    market_url_append(url, market_yahoo_symbol(b.syms[i]));
  }
  if (url.size() == base)
    return "";
  return url + "&" + kYahooRange;
}

inline std::string market_chart_url(const MarketSymbol &s) {
  std::string url = kYahooChartUrl;
  market_url_append(url, market_yahoo_symbol(s));
  return url + "?" + kYahooRange;
}

// The coins Kraken lists; "" without one.
inline std::string market_kraken_url(const MarketBlob &b) {
  std::string pairs;
  for (uint8_t i = 0; i < market_count(b); i++) {
    const char *p = market_kraken_pair(b.syms[i]);
    if (*p == 0)
      continue;
    if (!pairs.empty())
      pairs += ',';
    pairs += p;
  }
  return pairs.empty() ? pairs : kKrakenUrl + pairs;
}

// Nasdaq wants stock or ETF named, so asks both.
inline std::string market_nasdaq_url(const MarketBlob &b) {
  std::string q;
  for (uint8_t i = 0; i < market_count(b); i++) {
    const std::string sym = market_nasdaq_symbol(b.syms[i]);
    if (sym.empty())
      continue;
    const bool index = b.syms[i].kind == (uint8_t) MarketKind::INDEX;
    for (const char *cls : {index ? "index" : "stocks", index ? "" : "etf"}) {
      if (*cls == 0)
        continue;
      q += q.empty() ? "symbol=" : "&symbol=";
      q += sym + "%7C" + cls;
    }
  }
  return q.empty() ? q : kNasdaqUrl + q;
}

// ---- quotes and market hours ----------------------------------------------------

struct MarketQuote {
  MarketQuote() { memset(this->spark, kSparkEmpty, sizeof this->spark); }
  double price{0};
  double prev{0};    // last close; crypto the UTC day's open
  uint32_t time{0};  // last trade or read, epoch seconds
  // The session in epoch seconds; 0 unknown
  uint32_t pre_at{0}, open_at{0}, close_at{0}, post_at{0};
  int32_t utc_offset{0};  // the exchange's, seconds
  float spark_lo{0}, spark_hi{0};
  bool valid{false};
  uint8_t state{0};  // the source's word, without session times
  uint8_t spark[kSparkPoints];
};

inline MarketState market_state(const MarketQuote &q, MarketKind kind, int64_t now) {
  if (kind == MarketKind::CRYPTO)
    return MarketState::ALWAYS;
  if (!q.valid)
    return MarketState::UNKNOWN;
  if (q.open_at == 0 || q.close_at <= q.open_at)
    return q.state <= (uint8_t) MarketState::CLOSED ? (MarketState) q.state : MarketState::UNKNOWN;
  const int64_t pre = q.pre_at != 0 && q.pre_at < q.open_at ? q.pre_at : q.open_at;
  const int64_t post = q.post_at > q.close_at ? q.post_at : q.close_at;
  if (now < pre || now >= post)
    return MarketState::CLOSED;
  if (now < (int64_t) q.open_at)
    return MarketState::PRE;
  return now < (int64_t) q.close_at ? MarketState::OPEN : MarketState::POST;
}

inline const char *market_state_key(MarketState s) {
  static const char *const kNames[] = {"", "pre", "open", "post", "closed", "always"};
  return (uint8_t) s <= (uint8_t) MarketState::ALWAYS ? kNames[(uint8_t) s] : "";
}

// Nasdaq's marketStatus words, like "Market Closed".
inline MarketState market_state_word(const std::string &w) {
  if (w.find("Pre") != std::string::npos)
    return MarketState::PRE;
  if (w.find("After") != std::string::npos)
    return MarketState::POST;
  if (w.find("Open") != std::string::npos)
    return MarketState::OPEN;
  if (w.find("Close") != std::string::npos)
    return MarketState::CLOSED;
  return MarketState::UNKNOWN;
}

constexpr int64_t kMarketStaleSeconds = 30 * 60;

// Trading now, yet no new price lately.
inline bool market_stale(const MarketQuote &q, MarketKind kind, int64_t now) {
  const MarketState st = market_state(q, kind, now);
  return q.valid && (st == MarketState::OPEN || st == MarketState::ALWAYS) &&
         now - (int64_t) q.time > kMarketStaleSeconds;
}

constexpr uint32_t kMarketPollMs = 60 * 1000;
constexpr uint32_t kMarketShutPollMs = 5 * 60 * 1000;
constexpr uint32_t kMarketClosedPollMs = 10 * 60 * 1000;

// Coins or open markets each minute; never past the open
inline uint32_t market_poll_ms(const MarketBlob &b, const MarketQuote (&q)[kMarketSlots], int64_t now) {
  uint32_t ms = kMarketClosedPollMs;
  for (uint8_t i = 0; i < market_count(b); i++) {
    const MarketState st = market_state(q[i], (MarketKind) b.syms[i].kind, now);
    if (st == MarketState::OPEN || st == MarketState::ALWAYS || st == MarketState::UNKNOWN)
      return kMarketPollMs;
    if (st != MarketState::CLOSED && ms > kMarketShutPollMs)
      ms = kMarketShutPollMs;
    // Just after a known open
    const int64_t until = (int64_t) q[i].open_at - now;
    if (q[i].open_at != 0 && until > 0 && until * 1000 + 5000 < (int64_t) ms)
      ms = (uint32_t) (until * 1000 + 5000);
  }
  return ms;
}

// The stocks' state for a title; slot -1 none.
struct MarketBoardState {
  MarketState state{MarketState::UNKNOWN};
  int slot{-1};
};

inline MarketBoardState market_board_state(const MarketBlob &b, const MarketQuote (&q)[kMarketSlots], int64_t now) {
  static const MarketState kRank[] = {MarketState::OPEN, MarketState::PRE, MarketState::POST, MarketState::CLOSED};
  MarketBoardState best;
  int best_rank = 4;
  for (uint8_t i = 0; i < market_count(b); i++) {
    const MarketState st = market_state(q[i], (MarketKind) b.syms[i].kind, now);
    for (int r = 0; r < best_rank; r++)
      if (kRank[r] == st) {
        best = MarketBoardState{st, i};
        best_rank = r;
      }
  }
  return best;
}

// ---- the sparkline buckets ------------------------------------------------------

// The bucket a time falls in; -1 before start.
inline int market_spark_bucket(int64_t t, int64_t start, int64_t end) {
  if (end <= start || t < start)
    return -1;
  const int64_t b = (t - start) * kSparkPoints / (end - start);
  return b >= kSparkPoints ? kSparkPoints - 1 : (int) b;
}

// Bucket prices, NAN for none, packed into q.
inline void market_spark_pack(MarketQuote &q, const float (&v)[kSparkPoints]) {
  float lo = INFINITY, hi = -INFINITY;
  for (float x : v)
    if (std::isfinite(x)) {
      lo = x < lo ? x : lo;
      hi = x > hi ? x : hi;
    }
  memset(q.spark, kSparkEmpty, sizeof q.spark);
  q.spark_lo = q.spark_hi = 0;
  if (lo > hi)
    return;
  q.spark_lo = lo;
  q.spark_hi = hi;
  const double span = (double) hi - lo;
  for (int i = 0; i < kSparkPoints; i++)
    if (std::isfinite(v[i]))
      q.spark[i] = span > 0 ? (uint8_t) std::lround((v[i] - (double) lo) / span * kSparkTop) : kSparkTop / 2;
}

inline double market_spark_value(const MarketQuote &q, int i) {
  return q.spark_lo + ((double) q.spark_hi - q.spark_lo) * q.spark[i] / kSparkTop;
}

// ---- prices as text -------------------------------------------------------------

// 2 decimals from 1 up; 4 significant digits below.
inline int market_decimals(double price) {
  double a = std::fabs(price);
  if (a >= 1 || a == 0 || !std::isfinite(a))
    return 2;
  int d = 4;
  while (a < 0.1 && d < 8) {
    a *= 10;
    d++;
  }
  return d;
}

// sign puts "+" on positives; rounded zero has none.
inline std::string market_number(double v, int decimals, bool commas, bool sign) {
  if (!std::isfinite(v))
    return "--";
  char buf[48];
  const double a = std::fabs(v);
  snprintf(buf, sizeof buf, "%.*f", decimals, a < 1e15 ? a : 1e15);
  std::string digits(buf);
  const size_t dot = digits.find('.');
  const size_t whole = dot == std::string::npos ? digits.size() : dot;
  if (commas)
    for (size_t at = whole; at > 3; at -= 3)
      digits.insert(at - 3, 1, ',');
  if (digits.find_first_of("123456789") == std::string::npos)
    return digits;
  return (v < 0 ? "-" : sign ? "+" : "") + digits;
}

// "52.5K", "1.2M": the last try for big numbers.
inline std::string market_short(double v, bool sign) {
  const double a = std::fabs(v);
  const char *unit = a >= 1e9 ? "B" : a >= 1e6 ? "M" : "K";
  const double scaled = a >= 1e9 ? v / 1e9 : a >= 1e6 ? v / 1e6 : v / 1e3;
  return market_number(scaled, std::fabs(scaled) < 100 ? 1 : 0, false, sign) + unit;
}

// Fuller forms first; the shortest when none fits.
inline std::string market_fit(double v, int decimals, size_t max, bool sign) {
  std::string out = market_number(v, decimals, true, sign);
  if (out.size() <= max || !std::isfinite(v))
    return out;
  if (std::fabs(v) < 1) {
    // Never rounds a move away to zero.
    for (int d = decimals - 1; d >= 2 && out.size() > max; d--) {
      const std::string s = market_number(v, d, false, sign);
      if (s.find_first_of("123456789") == std::string::npos)
        break;
      out = s;
    }
    return out;
  }
  for (int d : {decimals, 0})
    for (bool commas : {true, false}) {
      out = market_number(v, d, commas, sign);
      if (out.size() <= max)
        return out;
    }
  if (std::fabs(v) >= 1000) {
    const std::string s = market_short(v, sign);
    if (s.size() < out.size())
      out = s;
  }
  return out;
}

// "73,738.65", narrower "73738.65", "73,739", "73739", "74K".
inline std::string market_price_text(double price, size_t max) {
  return market_fit(price, market_decimals(price), max, false);
}

// At the price's decimals, signed: "+7.12", "-0.00035".
inline std::string market_change_text(double change, double price, size_t max) {
  return market_fit(change, market_decimals(price), max, true);
}

// "+2.04%", "-12.3%", "+250%"; fewer decimals to fit.
inline std::string market_percent_text(double pct, size_t max) {
  if (!std::isfinite(pct))
    return "";
  const double a = std::fabs(pct);
  std::string out;
  for (int d = a < 10 ? 2 : a < 100 ? 1 : 0; d >= 0; d--) {
    out = market_number(pct, d, false, true) + "%";
    if (out.size() <= max)
      break;
  }
  return out;
}

inline double market_change(const MarketQuote &q) { return q.valid && q.prev != 0 ? q.price - q.prev : 0; }

inline double market_percent(const MarketQuote &q) {
  return q.valid && q.prev != 0 ? (q.price - q.prev) / std::fabs(q.prev) * 100 : NAN;
}

// Up, down or flat at the price's decimals.
inline int market_dir(const MarketQuote &q) {
  if (!q.valid || q.prev == 0)
    return 0;
  const std::string s = market_number(market_change(q), market_decimals(q.price), false, true);
  return s[0] == '+' ? 1 : s[0] == '-' ? -1 : 0;
}

// The ticker's bet colors; flat and stale stay plain.
constexpr uint32_t kMarketUp = kBetGood, kMarketDown = kBetBad;
constexpr uint32_t kMarketFlat = 0xFFFFFF, kMarketStale = 0x808080;

inline uint32_t market_color(int dir) { return dir > 0 ? kMarketUp : dir < 0 ? kMarketDown : kMarketFlat; }

// "CLOSED", or "FRI CLOSE" after a weekend or holiday.
inline std::string market_state_text(MarketState st, const MarketQuote &q, int64_t now, bool wide) {
  switch (st) {
    case MarketState::PRE:
      return wide ? "PRE-MARKET" : "PRE";
    case MarketState::OPEN:
      return "OPEN";
    case MarketState::POST:
      return wide ? "AFTER HOURS" : "AH";
    case MarketState::CLOSED: {
      static const char *const kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
      const int64_t last = ((int64_t) q.time + q.utc_offset) / 86400, today = (now + q.utc_offset) / 86400;
      if (q.time == 0 || last >= today || today - last > 6)
        return "CLOSED";
      const std::string day = kDays[((last + 4) % 7 + 7) % 7];
      return wide ? day + " CLOSE" : day;
    }
    default:
      return "";
  }
}

}  // namespace espn
