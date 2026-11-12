// Stocks & Crypto reads: Yahoo, Kraken and Nasdaq.
#pragma once

#include "espn_parse.h"
#include "markets.h"

namespace espn {

namespace detail {
void fill_yahoo_filter(JsonDocument &f);
void fill_kraken_filter(JsonDocument &f);
void fill_nasdaq_filter(JsonDocument &f);
int yahoo_from_doc(JsonObjectConst root, const MarketBlob &list, MarketQuote (&out)[kMarketSlots]);
int kraken_from_doc(JsonObjectConst root, const MarketBlob &list, int64_t now, MarketQuote (&out)[kMarketSlots]);
int nasdaq_from_doc(JsonObjectConst root, const MarketBlob &list, int64_t now, MarketQuote (&out)[kMarketSlots]);

template<typename TInput, typename F> int market_read(TInput &input, void (*fill)(JsonDocument &), F from) {
  JsonDocument filter(json_allocator());
  fill(filter);
  JsonDocument doc(json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return -1;
  return from(doc.as<JsonObjectConst>());
}
}  // namespace detail

// Yahoo's spark or chart: quotes found, -1 unreadable.
template<typename TInput>
int parse_yahoo_quotes(TInput &input, const MarketBlob &list, MarketQuote (&out)[kMarketSlots]) {
  return detail::market_read(input, detail::fill_yahoo_filter,
                             [&](JsonObjectConst root) { return detail::yahoo_from_doc(root, list, out); });
}

// Kraken has no trade time: now stands in.
template<typename TInput>
int parse_kraken_quotes(TInput &input, const MarketBlob &list, int64_t now, MarketQuote (&out)[kMarketSlots]) {
  return detail::market_read(input, detail::fill_kraken_filter,
                             [&](JsonObjectConst root) { return detail::kraken_from_doc(root, list, now, out); });
}

template<typename TInput>
int parse_nasdaq_quotes(TInput &input, const MarketBlob &list, int64_t now, MarketQuote (&out)[kMarketSlots]) {
  return detail::market_read(input, detail::fill_nasdaq_filter,
                             [&](JsonObjectConst root) { return detail::nasdaq_from_doc(root, list, now, out); });
}

}  // namespace espn
