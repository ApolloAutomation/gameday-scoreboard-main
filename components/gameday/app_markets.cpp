// Built for size (-Os): frames run every few seconds.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
#include "app_markets.h"

#include "source_markets.h"

namespace espn {

void markets_frame(const MarketBlob &b, const MarketQuote (&q)[kMarketSlots], int w, int h, int64_t now,
                   AppFrame &out) {
  const uint8_t n = market_count(b);
  int charted[kMarketSlots];
  int k = 0;
  for (uint8_t i = 0; i < n; i++)
    if (q[i].valid && market_spark_count(q[i]) >= 2)
      charted[k++] = i;
  const MarketGeom g = market_geom(w, h, true, k > 0, market_logos_fit(b, w), market_any_logo(b));
  const int64_t tick = now > 0 ? now / kMarketCardSeconds : 0;
  MarketBlob view = b;
  uint8_t page = 0;
  if (g.spark && k > 0) {
    // Each charted symbol in turn, on its page
    const int s = charted[tick % k];
    view.spark = (uint8_t) (s + 1);
    page = (uint8_t) (s / g.rows);
  } else {
    const int pages = n == 0 ? 1 : (n + g.rows - 1) / g.rows;
    page = (uint8_t) (tick % pages);
  }
  const bool alt = now > 0 && (now / kMarketAltSeconds) % 2 == 1;
  const MarketFrame f = market_frame(view, q, g, now, page, alt);

  BoardLines &r = out.rows;
  r.title = f.title;
  r.status = f.status;
  r.status_color = f.status_color;
  const MarketCol none{};
  const MarketCol &pos = g.cols == 4 ? g.price : none;
  const MarketCol &value = g.cols == 4 ? g.change : g.price;
  const MarketCol &detail = g.cols >= 3 ? g.pct : none;
  r.cols = {pos.x, pos.w, g.sym.x, g.sym.w, value.x, value.w, detail.x, detail.w};
  for (uint8_t i = 0; i < f.n; i++) {
    const MarketRow &row = f.rows[i];
    const bool four = g.cols == 4;
    r.pos.push_back(four ? row.price : "");
    r.name.push_back(row.sym);
    r.value.push_back(four ? row.change : row.price);
    r.detail.push_back(row.pct);
    r.pos_color.push_back((int) kMarketFlat);
    r.name_color.push_back((int) (f.spark && !row.spark ? kMarketDim : kMarketFlat));
    r.value_color.push_back((int) (g.cols == 3 ? kMarketFlat : row.color));
    r.detail_color.push_back((int) row.color);
  }
  // The charted row stands out: a bar behind it
  if (f.active >= 0)
    out.bar = AppFill{g.list.x, (int16_t) (g.row_y + f.active * kMarketRowPx), g.list.w, (int16_t) kMarketRowPx,
                      kMarketBar};
  auto logo = [&](const MarketSymbol &s, int x, int y, int px) -> AppIcon & {
    AppIcon ic;
    ic.x = (int16_t) x;
    ic.y = (int16_t) y;
    ic.px = (uint8_t) px;
    ic.kind = s.kind;
    memcpy(ic.key, s.sym, sizeof s.sym);
    ic.url = market_logo_url;
    out.icons.push_back(ic);
    return out.icons.back();
  };
  auto text = [&](int x, int y, int w, MarketFont font, uint32_t color, std::string s) -> int8_t {
    AppText t;
    t.x = (int16_t) x;
    t.y = (int16_t) y;
    t.w = (int16_t) w;
    t.font = font;
    t.color = color;
    t.text = std::move(s);
    out.texts.push_back(std::move(t));
    return (int8_t) (out.texts.size() - 1);
  };
  using esphome::gameday::font_px;
  // Its logo big, its name there until it draws
  if (f.slot >= 0 && g.focus_px > 0) {
    const MarketSymbol &s = b.syms[f.slot];
    const int px = g.focus_px;
    // Five letters reach the line's edge
    std::string label = market_sym_text(s.sym, (size_t) ((px + 2) / kMarketCharPx));
    const MarketFont font = (int) label.size() * font_px(MarketFont::S12) <= px ? MarketFont::S12 : MarketFont::S8;
    const int tw = (int) label.size() * font_px(font);
    const int8_t alt = text(g.focus_x, g.focus_y + (px - esphome::gameday::font_rows(font)) / 2, std::max(tw, px), font,
                            kMarketFlat, std::move(label));
    out.texts[alt].align = AppAlign::CENTER;
    int8_t with = -1;
    // Four panels: name beside the drawn logo, move always
    if (!f.name.empty() && f.logo) {
      const bool big = (int) f.name.size() * font_px(MarketFont::S12) <= g.name_at.w;
      with = text(g.name_at.x, g.name_at.y, g.name_at.w, big ? MarketFont::S12 : MarketFont::S8, kMarketFlat, f.name);
    }
    if (!f.move.empty())
      text(g.move_at.x, g.move_at.y, g.move_at.w, MarketFont::S8, f.move_color, f.move);
    if (f.logo) {
      AppIcon &big = logo(s, g.focus_x, g.focus_y, px);
      big.alt = alt;
      big.with = with;
    }
  }
  if (g.logo.w > 0) {
    const int first = f.page * g.rows;
    for (uint8_t i = 0; i < f.n; i++) {
      const MarketSymbol &s = b.syms[first + i];
      if (market_has_logo(s.sym, s.kind))
        logo(s, g.logo.x, g.row_y + i * kMarketRowPx, kMarketLogoPx);
    }
  }
  if (f.spark) {
    for (uint8_t i = 0; i < f.line.n; i++)
      out.line.pts.push_back(AppPoint{f.line.pts[i].x, f.line.pts[i].y});
    out.line.color = f.line.color;
  }
  out.ticker = market_ticker(b, q, now, &out.spans);
}

MarketsApp::MarketsApp() : defaults_(market_defaults()) {}

const char *const *MarketsApp::hosts() const {
  static const char *const kHosts[] = {"query1.finance.yahoo.com", "api.kraken.com", "api.nasdaq.com", nullptr};
  return kHosts;
}

void MarketsApp::setup() { this->list_.loaded(); }

void MarketsApp::follow_(const MarketBlob &was) {
  AppVector<MarketQuote> moved(kMarketSlots);
  bool miss[kMarketSlots]{};
  const MarketBlob &b = this->symbols();
  for (uint8_t i = 0; i < market_count(b); i++) {
    const uint8_t from = market_find(was, b.syms[i]);
    if (from != 0) {
      moved[i] = this->quotes_[from - 1];
      miss[i] = this->miss_[from - 1];
    }
  }
  for (int i = 0; i < kMarketSlots; i++) {
    this->quotes_[i] = moved[i];
    this->miss_[i] = miss[i];
  }
}

AppSet MarketsApp::set(const std::string &key, const std::string &value) {
  const MarketBlob was = this->symbols();
  const AppSet r = markets_set(this->list_, this->defaults_, key, value);
  if (r == AppSet::CHANGED) {
    this->follow_(was);
    this->feed_.nasdaq = this->feed_.now = false;
  }
  return r;
}

void MarketsApp::state(JsonObject out, int64_t now) const {
  const MarketBlob &b = this->symbols();
  JsonArray syms = out["syms"].to<JsonArray>();
  for (uint8_t i = 0; i < market_count(b); i++) {
    const MarketSymbol &s = b.syms[i];
    const MarketQuote &q = this->quotes_[i];
    JsonObject o = syms.add<JsonObject>();
    o["slot"] = i + 1;
    o["sym"] = (const char *) s.sym;
    o["kind"] = market_kind_key((MarketKind) s.kind);
    if (!q.valid) {
      if (this->miss_[i])
        o["miss"] = true;
      continue;
    }
    o["price"] = market_price_text(q.price, 12);
    if (q.prev != 0) {
      o["chg"] = market_change_text(market_change(q), q.price, 12);
      o["pct"] = market_percent_text(market_percent(q), 8);
    }
    o["dir"] = market_dir(q);
    o["st"] = market_state_key(market_state(q, (MarketKind) s.kind, now != 0 ? now : this->read_at_));
  }
  out["max"] = kMarketSlots;
  out["defaults"] = market_count(this->list_.blob) == 0;
  // "retry": Yahoo is down and no backup covers these.
  const bool covered = !market_kraken_url(b).empty() || !market_nasdaq_url(b).empty();
  out["src"] = !this->feed_.backup ? "yahoo" : covered ? "backup" : "retry";
  if (this->read_at_ != 0)
    out["read"] = this->read_at_;
}

bool MarketsApp::busy(int64_t now) const {
  const MarketBlob &b = this->symbols();
  for (uint8_t i = 0; i < market_count(b); i++)
    if (market_state(this->quotes_[i], (MarketKind) b.syms[i].kind, now) == MarketState::OPEN)
      return true;
  return false;
}

uint32_t MarketsApp::poll_ms(int64_t now) const {
  return this->feed_.now ? 0 : market_poll_ms(this->symbols(), this->quotes_, now, this->miss_);
}

// Under the night clock: watch for an open.
uint32_t MarketsApp::night_poll_ms(int64_t /*now*/) const {
  const MarketBlob &b = this->symbols();
  for (uint8_t i = 0; i < market_count(b); i++)
    if (b.syms[i].kind != (uint8_t) MarketKind::CRYPTO)
      return kMarketClosedPollMs;
  return 0;
}

bool MarketsApp::request(int64_t now, AppRequest &out) {
  const MarketBlob &b = this->symbols();
  this->asked_ = b;
  this->asked_at_ = now;
  this->src_ = this->feed_.next(b, now);
  this->feed_.now = false;
  out.url = this->src_ == MarketSource::KRAKEN   ? market_kraken_url(b)
            : this->src_ == MarketSource::NASDAQ ? market_nasdaq_url(b)
                                                 : market_spark_url(b);
  // Yahoo and Nasdaq turn curl away
  out.headers.push_back(AppHeader{"User-Agent", kMarketsUserAgent});
  return !out.url.empty();
}

bool MarketsApp::filter(JsonDocument &f) {
  switch (this->src_) {
    case MarketSource::KRAKEN:
      detail::fill_kraken_filter(f);
      break;
    case MarketSource::NASDAQ:
      detail::fill_nasdaq_filter(f);
      break;
    default:
      detail::fill_yahoo_filter(f);
      break;
  }
  return true;
}

bool MarketsApp::parse_doc(JsonObjectConst root, int64_t now) {
  for (MarketQuote &r : this->read_)
    r = MarketQuote();
  int n = -1;
  switch (this->src_) {
    case MarketSource::KRAKEN:
      n = detail::kraken_from_doc(root, this->asked_, now, this->read_);
      break;
    case MarketSource::NASDAQ:
      n = detail::nasdaq_from_doc(root, this->asked_, now, this->read_);
      break;
    default:
      n = detail::yahoo_from_doc(root, this->asked_, this->read_);
      break;
  }
  return n >= 0;
}

void MarketsApp::apply(bool ok, int64_t now) {
  if (!ok)
    return;  // failed() took it
  this->failing_ = false;
  for (uint8_t i = 0; i < market_count(this->asked_); i++) {
    if (this->read_[i].valid) {
      this->quotes_[i] = market_merge(this->quotes_[i], this->read_[i], now);
      this->miss_[i] = false;
    } else if (this->src_ == MarketSource::YAHOO) {
      this->miss_[i] = true;
    }
  }
  this->read_at_ = now;
  this->feed_.done(this->src_, true, false, this->asked_, now);
}

bool MarketsApp::failed(bool refused) {
  this->failing_ = true;
  return this->feed_.done(this->src_, false, refused, this->asked_, this->asked_at_, this->retry_after_ms_);
}

void MarketsApp::frame(int w, int h, int64_t now, AppFrame &out) const {
  markets_frame(this->symbols(), this->quotes_, w, h, now, out);
  if (!out.ticker.empty())
    return;
  // No price yet: say why
  out.ticker = !this->failing_      ? "Reading prices"
               : this->feed_.backup ? "Can't reach Yahoo Finance, Kraken or Nasdaq"
                                    : "Can't reach Yahoo Finance";
}

}
