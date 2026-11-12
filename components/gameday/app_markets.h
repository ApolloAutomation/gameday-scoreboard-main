// Stocks & Crypto as a display; pure, for host tests.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "apps.h"
#include "markets_view.h"

namespace espn {

// ---- the owner's list -----------------------------------------------------------

// Shown while the owner has saved none.
inline MarketBlob market_defaults() {
  MarketBlob b{};
  for (const char *t : {"SPY", "AAPL", "BTC", "ETH"}) {
    MarketSymbol s{};
    if (parse_market_symbol(t, MarketKind::NONE, s))
      market_put(b, 0, s);
  }
  return b;
}

inline const MarketBlob &market_shown(const MarketBlob &saved, const MarketBlob &defaults) {
  return market_count(saved) != 0 ? saved : defaults;
}

static const char *const kMarketSetKeys[] = {"mkt", "mktdel", "mktmove", nullptr};

// mkt "slot,symbol[,kind]", mktdel slot, mktmove "from,to"; edits what shows.
inline AppSet markets_set(MarketList &list, const MarketBlob &defaults, const std::string &key,
                          const std::string &value) {
  const MarketBlob &was = market_shown(list.blob, defaults);
  MarketBlob work = was;
  bool ok = false;
  uint8_t a = 0, b = 0;
  if (key == "mkt") {
    MarketSymbol s{};
    ok = parse_market(value, a, s) && market_put(work, a, s) != 0;
  } else if (key == "mktdel") {
    ok = parse_market_slot(value, a) && market_del(work, a);
  } else if (key == "mktmove") {
    ok = parse_market_move(value, a, b) && market_move(work, a, b);
  }
  if (!ok)
    return AppSet::REFUSED;
  // Untouched defaults stay unsaved
  if (memcmp(&work, &was, sizeof work) == 0)
    return AppSet::SAME;
  list.blob = work;
  list.dirty = true;
  return AppSet::CHANGED;
}

// ---- which host reads ------------------------------------------------------------

enum class MarketSource : uint8_t { YAHOO = 0, KRAKEN, NASDAQ };

inline const char *market_source_key(MarketSource s) {
  return s == MarketSource::KRAKEN ? "kraken" : s == MarketSource::NASDAQ ? "nasdaq" : "yahoo";
}

constexpr uint8_t kMarketFailover = 2;  // Yahoo failures before the backups
constexpr int64_t kMarketRetrySeconds = 10 * 60;
constexpr int64_t kMarketRefusedSeconds = 30 * 60;
constexpr int64_t kMarketRetryCapSeconds = 60 * 60;

// Yahoo first; Kraken and Nasdaq while it fails.
struct MarketFeed {
  bool backup{false};
  uint8_t fails{0};    // Yahoo's, in a row
  uint8_t retries{0};  // failed returns to Yahoo, in a row
  int64_t retry_at{0};
  int64_t rest[3]{};   // a refusing backup's quiet time
  bool nasdaq{false};  // the backup's second read is next
  bool now{false};     // the next read skips the wait

  MarketSource next(const MarketBlob &b, int64_t t) const {
    if (!this->backup || t >= this->retry_at)
      return MarketSource::YAHOO;
    const bool k = t >= this->rest[1] && !market_kraken_url(b).empty();
    const bool n = t >= this->rest[2] && !market_nasdaq_url(b).empty();
    if (this->nasdaq && n)
      return MarketSource::NASDAQ;
    if (k)
      return MarketSource::KRAKEN;
    return n ? MarketSource::NASDAQ : MarketSource::YAHOO;
  }

  // After each read; true reads the next now.
  bool done(MarketSource src, bool ok, bool refused, const MarketBlob &b, int64_t t) {
    this->nasdaq = false;
    if (src != MarketSource::YAHOO) {
      if (refused)
        this->rest[(int) src] = t + kMarketRetrySeconds;
      // Kraken, then Nasdaq straight after
      this->nasdaq = src == MarketSource::KRAKEN && t >= this->rest[2] && !market_nasdaq_url(b).empty();
      return this->now = this->nasdaq;
    }
    if (ok) {
      this->backup = false;
      this->fails = this->retries = 0;
      return this->now = false;
    }
    if (this->fails < 255)
      this->fails++;
    if (!refused && !this->backup && this->fails < kMarketFailover)
      return this->now = false;
    if (this->backup && this->retries < 3)
      this->retries++;
    this->backup = true;
    int64_t hold = refused ? kMarketRefusedSeconds : kMarketRetrySeconds;
    for (uint8_t i = 0; i < this->retries && hold < kMarketRetryCapSeconds; i++)
      hold *= 2;
    this->retry_at = t + (hold < kMarketRetryCapSeconds ? hold : kMarketRetryCapSeconds);
    return this->now = this->next(b, t) != MarketSource::YAHOO;
  }
};

// ---- quotes between reads --------------------------------------------------------

inline int market_spark_count(const MarketQuote &q) {
  int n = 0;
  for (uint8_t v : q.spark)
    n += v != kSparkEmpty ? 1 : 0;
  return n;
}

// A backup's quote keeps the day's sparkline, plus its price.
inline MarketQuote market_merge(const MarketQuote &shown, const MarketQuote &read, int64_t now) {
  MarketQuote q = read;
  if (!read.valid || market_spark_count(read) != 0 || !shown.valid || market_spark_count(shown) == 0)
    return q;
  const int64_t from = shown.pre_at != 0 && shown.pre_at < shown.open_at ? shown.pre_at : shown.open_at;
  const int64_t to = shown.post_at > shown.close_at ? shown.post_at : shown.close_at;
  if (shown.open_at == 0 || shown.close_at <= shown.open_at || now < from || now >= to)
    return q;
  q.pre_at = shown.pre_at;
  q.open_at = shown.open_at;
  q.close_at = shown.close_at;
  q.post_at = shown.post_at;
  q.utc_offset = shown.utc_offset;
  float v[kSparkPoints];
  for (int i = 0; i < kSparkPoints; i++)
    v[i] = shown.spark[i] == kSparkEmpty ? NAN : (float) market_spark_value(shown, i);
  const int at = now < (int64_t) shown.close_at ? market_spark_bucket(now, shown.open_at, shown.close_at) : -1;
  if (at >= 0 && std::isfinite(read.price))
    v[at] = (float) read.price;
  market_spark_pack(q, v);
  return q;
}

// ---- what the board draws --------------------------------------------------------

constexpr int64_t kMarketCardSeconds = 8;  // the sparkline's symbol moves on
constexpr int64_t kMarketAltSeconds = 4;   // one panel: price, then percent
constexpr uint32_t kMarketDim = 0xA0A0A0;

// Rows, the rotating symbol's sparkline and the ticker.
void markets_frame(const MarketBlob &b, const MarketQuote (&q)[kMarketSlots], int w, int h, int64_t now,
                   AppFrame &out);

// The display itself; markets_set and MarketFeed do the deciding.
class MarketsApp final : public App {
 public:
  MarketsApp();
  Display id() const override { return Display::MARKETS; }
  const char *const *hosts() const override;
  void *prefs() override { return &this->list_.blob; }
  size_t prefs_size() const override { return sizeof(MarketBlob); }
  void setup() override;
  const char *const *set_keys() const override { return kMarketSetKeys; }
  AppSet set(const std::string &key, const std::string &value) override;
  void state(JsonObject out) const override;
  // A listed market's trading day, as a game is on
  bool busy(int64_t now) const override;
  uint32_t poll_ms(int64_t now) const override;
  bool request(int64_t now, AppRequest &out) override;
  bool filter(JsonDocument &f) override;
  bool parse_doc(JsonObjectConst root, int64_t now) override;
  void apply(bool ok, int64_t now) override;
  bool failed(bool refused) override;
  void frame(int w, int h, int64_t now, AppFrame &out) const override;
  // Each second, so every turn lands on time
  uint32_t redraw_ms() const override { return 1000; }

  const MarketBlob &symbols() const { return market_shown(this->list_.blob, this->defaults_); }
  const MarketQuote &quote(int slot) const { return this->quotes_[slot]; }
  const MarketFeed &feed() const { return this->feed_; }

 private:
  // Quotes follow their symbols to new slots.
  void follow_(const MarketBlob &was);

  MarketList list_;
  MarketBlob defaults_;
  MarketQuote quotes_[kMarketSlots];
  bool miss_[kMarketSlots]{};  // Yahoo answered without it
  MarketFeed feed_;
  // The read in flight; parse_doc writes read_
  MarketBlob asked_{};
  MarketSource src_{MarketSource::YAHOO};
  int64_t asked_at_{0};
  MarketQuote read_[kMarketSlots];
  int64_t read_at_{0};  // the last good read
  bool failing_{false};
};

}  // namespace espn
