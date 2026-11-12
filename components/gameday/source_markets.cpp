// Built for size (-Os): it runs once a poll.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
#include "source_markets.h"

#include <cmath>
#include <cstdlib>
#include <strings.h>

namespace espn {
namespace detail {

namespace {

bool same_text(const std::string &a, const std::string &b) { return !a.empty() && strcasecmp(a.c_str(), b.c_str()) == 0; }

// A number, or text like "$355.63" or "25,466.19".
double number(JsonVariantConst v) {
  if (v.is<double>() || v.is<long long>())
    return v.as<double>();
  const char *s = v.as<const char *>();
  if (s == nullptr)
    return NAN;
  std::string t;
  for (; *s != 0 && t.size() < 32; s++)
    if (*s != '$' && *s != ',')
      t += *s;
  char *end = nullptr;
  const double d = strtod(t.c_str(), &end);
  return !t.empty() && end != nullptr && *end == 0 ? d : NAN;
}

uint32_t epoch(JsonVariantConst v) {
  const long long n = v.is<long long>() ? v.as<long long>() : 0;
  return n > 0 && n <= (long long) UINT32_MAX ? (uint32_t) n : 0;
}

}  // namespace

void fill_yahoo_filter(JsonDocument &f) {
  auto series = [](JsonObject r) {
    JsonObject m = r["meta"].to<JsonObject>();
    keep(m, {"symbol", "regularMarketPrice", "previousClose", "chartPreviousClose", "regularMarketTime", "gmtoffset"});
    JsonObject p = m["currentTradingPeriod"].to<JsonObject>();
    for (const char *k : {"pre", "regular", "post"})
      keep(p[k].to<JsonObject>(), {"start", "end"});
    r["timestamp"] = true;
    r["indicators"]["quote"].add<JsonObject>()["close"] = true;
  };
  JsonObject s = f["spark"]["result"].add<JsonObject>();
  s["symbol"] = true;
  series(s["response"].add<JsonObject>());
  series(f["chart"]["result"].add<JsonObject>());
}

// Kraken keys each pair by its own name.
void fill_kraken_filter(JsonDocument &f) {
  f["error"] = true;
  keep(f["result"]["*"].to<JsonObject>(), {"c", "o"});
}

void fill_nasdaq_filter(JsonDocument &f) {
  keep(f["data"].add<JsonObject>(), {"symbol", "lastSalePrice", "previousClosePrice", "netChange", "marketStatus"});
  f["status"]["rCode"] = true;
}

namespace {

// One symbol's meta, times and closes.
bool yahoo_series(JsonObjectConst r, const MarketBlob &list, MarketQuote (&out)[kMarketSlots]) {
  JsonObjectConst meta = r["meta"];
  const std::string sym = text(meta, "symbol");
  int slot = -1;
  for (uint8_t i = 0; i < market_count(list) && slot < 0; i++)
    if (same_text(market_yahoo_symbol(list.syms[i]), sym))
      slot = i;
  if (slot < 0)
    return false;
  MarketQuote q;
  q.price = number(meta["regularMarketPrice"]);
  q.prev = number(meta["previousClose"]);
  if (!(q.prev > 0))
    q.prev = number(meta["chartPreviousClose"]);
  if (!std::isfinite(q.price))
    return false;
  if (!std::isfinite(q.prev))
    q.prev = 0;
  q.time = epoch(meta["regularMarketTime"]);
  q.utc_offset = clamp_int(meta["gmtoffset"], -16 * 3600, 16 * 3600, 0);
  JsonObjectConst p = meta["currentTradingPeriod"];
  q.pre_at = epoch(p["pre"]["start"]);
  q.open_at = epoch(p["regular"]["start"]);
  q.close_at = epoch(p["regular"]["end"]);
  q.post_at = epoch(p["post"]["end"]);
  JsonArrayConst ts = r["timestamp"], closes = r["indicators"]["quote"][0]["close"];
  int64_t start = q.open_at, end = q.close_at;
  if (end <= start && ts.size() > 1) {
    start = epoch(ts[0]);
    end = epoch(ts[ts.size() - 1]);
  }
  float v[kSparkPoints];
  for (float &x : v)
    x = NAN;
  auto c = closes.begin();
  for (JsonVariantConst t : ts) {
    if (c == closes.end())
      break;
    const double close = number(*c);
    ++c;
    const int b = market_spark_bucket(epoch(t), start, end);
    if (b >= 0 && std::fabs(close) < 1e30)
      v[b] = (float) close;
  }
  market_spark_pack(q, v);
  q.valid = true;
  out[slot] = q;
  return true;
}

}  // namespace

int yahoo_from_doc(JsonObjectConst root, const MarketBlob &list, MarketQuote (&out)[kMarketSlots]) {
  JsonObjectConst spark = root["spark"], chart = root["chart"];
  if (spark.isNull() && chart.isNull())
    return -1;
  int n = 0;
  for (JsonObjectConst r : spark["result"].as<JsonArrayConst>())
    n += yahoo_series(r["response"][0], list, out) ? 1 : 0;
  for (JsonObjectConst r : chart["result"].as<JsonArrayConst>())
    n += yahoo_series(r, list, out) ? 1 : 0;
  return n;
}

int kraken_from_doc(JsonObjectConst root, const MarketBlob &list, int64_t now, MarketQuote (&out)[kMarketSlots]) {
  JsonArrayConst err = root["error"];
  JsonObjectConst result = root["result"];
  if (err.size() > 0 || (err.isNull() && result.isNull()))
    return -1;
  const int64_t day = now - ((now % 86400) + 86400) % 86400;
  int n = 0;
  for (JsonPairConst kv : result) {
    for (uint8_t i = 0; i < market_count(list); i++) {
      if (strcmp(market_kraken_pair(list.syms[i]), kv.key().c_str()) != 0)
        continue;
      MarketQuote q;
      q.price = number(kv.value()["c"][0]);
      q.prev = number(kv.value()["o"]);
      if (!std::isfinite(q.price) || q.price <= 0)
        break;
      if (!std::isfinite(q.prev))
        q.prev = 0;
      q.time = now > 0 && now <= (int64_t) UINT32_MAX ? (uint32_t) now : 0;
      if (day > 0 && day + 86400 <= (int64_t) UINT32_MAX) {
        q.pre_at = q.open_at = (uint32_t) day;
        q.close_at = q.post_at = (uint32_t) (day + 86400);
      }
      q.state = (uint8_t) MarketState::ALWAYS;
      q.valid = true;
      out[i] = q;
      n++;
      break;
    }
  }
  return n;
}

int nasdaq_from_doc(JsonObjectConst root, const MarketBlob &list, int64_t now, MarketQuote (&out)[kMarketSlots]) {
  if (root["status"].isNull() && root["data"].isNull())
    return -1;
  int n = 0;
  for (JsonObjectConst d : root["data"].as<JsonArrayConst>()) {
    const std::string sym = text(d, "symbol");
    for (uint8_t i = 0; i < market_count(list); i++) {
      if (!same_text(market_nasdaq_symbol(list.syms[i]), sym))
        continue;
      MarketQuote q;
      q.price = number(d["lastSalePrice"]);
      q.prev = number(d["previousClosePrice"]);
      if (!std::isfinite(q.price))
        break;
      if (!std::isfinite(q.prev)) {
        const double change = number(d["netChange"]);
        q.prev = std::isfinite(change) ? q.price - change : 0;
      }
      q.time = now > 0 && now <= (int64_t) UINT32_MAX ? (uint32_t) now : 0;
      q.state = (uint8_t) market_state_word(text(d, "marketStatus"));
      q.valid = true;
      out[i] = q;
      n++;
      break;
    }
  }
  return n;
}

}  // namespace detail
}  // namespace espn
