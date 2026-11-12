// The Flight Tracker display (app_flights.h).
#include "app_flights.h"

#include <cstring>

#include "timezones.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace flights {

namespace {

const char *const kHosts[] = {"flightradar24.com", "adsb.fi", "adsb.lol", "adsb.im", "adsbdb.com",
                              "planespotters.net", nullptr};

uint32_t ms_of(int64_t now) { return (uint32_t) ((uint64_t) now * 1000u); }

// One signed decimal, rounded to 4 places.
bool read_deg(const char *&s, int32_t max_e4, int32_t &out) {
  bool neg = false;
  if (*s == '+' || *s == '-')
    neg = *s++ == '-';
  int64_t whole = 0;
  int digits = 0;
  for (; *s >= '0' && *s <= '9'; s++)
    if (++digits > 3)
      return false;
    else
      whole = whole * 10 + (*s - '0');
  if (digits < 1)
    return false;
  int64_t frac = 0;
  if (*s == '.') {
    s++;
    int64_t place = 10000;
    for (; *s >= '0' && *s <= '9'; s++, place /= 10)
      frac += (*s - '0') * place;
  }
  const int64_t e4 = (whole * 100000 + frac + 5) / 10;
  if (e4 > max_e4)
    return false;
  out = (int32_t) (neg ? -e4 : e4);
  return true;
}

bool space(char c) { return c == ' ' || c == '\t'; }

void copy_field(char *dst, size_t cap, const std::string &src) {
  const size_t n = std::min(src.size(), cap - 1);
  memcpy(dst, src.data(), n);
  dst[n] = '\0';
}

// Control characters out, cut on a UTF-8 boundary.
std::string clean_place(const std::string &v) {
  std::string out;
  for (unsigned char c : v)
    if (c >= 0x20 && c != 0x7F)
      out += (char) c;
  if (out.size() > kPlaceMax) {
    size_t n = kPlaceMax;
    while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80)
      n--;
    out.resize(n);
  }
  return out;
}

void put_text(espn::AppFrame &out, const Frame &fr) {
  out.texts.reserve(fr.items.size());
  for (const Text &t : fr.items) {
    espn::AppText a;
    a.x = t.x;
    a.y = t.y;
    a.w = t.w;
    a.font = t.font;
    a.align = (espn::AppAlign) t.align;
    a.color = t.color;
    a.text = t.text;
    out.texts.push_back(std::move(a));
  }
  for (const Art &a : fr.art) {
    const PlaneSprite *s = plane_sprite(a.kind);
    if (s == nullptr)
      continue;
    espn::AppArt o;
    o.x = a.x;
    o.y = a.y;
    o.w = s->w;
    o.h = s->h;
    o.bits = s->bits;
    o.flip = a.flip;
    o.ink[kArtBody - 1] = kColBody;
    o.ink[kArtTail - 1] = a.tail;
    o.ink[kArtDetail - 1] = kColDetail;
    out.art.push_back(o);
  }
  for (const Logo &l : fr.logos) {
    espn::AppIcon ic;
    ic.x = l.x;
    ic.y = l.y;
    ic.px = (uint8_t) kLogoPx;
    memcpy(ic.key, l.icao, sizeof l.icao);
    ic.url = logo_url;
    ic.alt = l.alt;
    ic.mark = true;
    out.icons.push_back(ic);
  }
  out.photo.x = fr.photo.x;
  out.photo.y = fr.photo.y;
  out.photo.w = fr.photo.w;
  out.photo.h = fr.photo.h;
  out.photo.url = fr.photo.url;
  out.photo.credit = fr.photo.credit;
  out.photo.at = fr.photo.at;
  out.ticker = fr.ticker;
}

}

// ---- Settings ----------------------------------------------------------------

uint16_t radius_max(DistUnit u) { return u == DistUnit::KM ? 400 : u == DistUnit::NM ? 200 : 250; }

const char *dist_unit_key(DistUnit u) { return u == DistUnit::KM ? "km" : u == DistUnit::NM ? "nm" : "mi"; }

Home prefs_home(const Prefs &p) {
  Home h;
  if (p.lat_e4 != kNoCoord && p.lon_e4 != kNoCoord) {
    h.lat = p.lat_e4 / 10000.0;
    h.lon = p.lon_e4 / 10000.0;
  }
  h.radius_nm = to_nm(p.radius, (DistUnit) p.radius_unit);
  return h;
}

void prefs_clean(Prefs &p) {
  const bool coords = p.lat_e4 != kNoCoord && p.lon_e4 != kNoCoord && p.lat_e4 >= -900000 && p.lat_e4 <= 900000 &&
                      p.lon_e4 >= -1800000 && p.lon_e4 <= 1800000 && !(p.lat_e4 == 0 && p.lon_e4 == 0);
  if (!coords)
    p.lat_e4 = p.lon_e4 = kNoCoord;
  if (p.radius_unit > (uint8_t) DistUnit::NM)
    p.radius_unit = 0;
  if (p.radius < 1 || p.radius > radius_max((DistUnit) p.radius_unit)) {
    p.radius = kRadiusDefault;
    p.radius_unit = 0;
  }
  p.place[kPlaceMax] = '\0';
  p.receiver[kReceiverMax] = '\0';
}

std::string coord_text(int32_t lat_e4, int32_t lon_e4) {
  if (lat_e4 == kNoCoord || lon_e4 == kNoCoord)
    return "";
  auto one = [](int32_t v) {
    char buf[16];
    const uint32_t a = (uint32_t) (v < 0 ? -(int64_t) v : v);
    snprintf(buf, sizeof(buf), "%s%u.%04u", v < 0 ? "-" : "", (unsigned) (a / 10000), (unsigned) (a % 10000));
    return std::string(buf);
  };
  return one(lat_e4) + "," + one(lon_e4);
}

bool parse_coords(const std::string &v, int32_t &lat_e4, int32_t &lon_e4) {
  const char *s = v.c_str();
  while (space(*s))
    s++;
  int32_t lat = 0, lon = 0;
  if (!read_deg(s, 900000, lat))
    return false;
  const char *gap = s;
  while (space(*s))
    s++;
  if (*s == ',')
    s++;
  else if (s == gap)
    return false;
  while (space(*s))
    s++;
  if (!read_deg(s, 1800000, lon))
    return false;
  while (space(*s))
    s++;
  if (*s != '\0' || (lat == 0 && lon == 0))
    return false;
  lat_e4 = lat;
  lon_e4 = lon;
  return true;
}

bool parse_radius(const std::string &v, uint16_t &n, uint8_t &unit) {
  size_t i = 0;
  uint32_t num = 0;
  for (; i < v.size() && i < 3 && v[i] >= '0' && v[i] <= '9'; i++)
    num = num * 10 + (uint32_t) (v[i] - '0');
  if (i == 0 || num < 1)
    return false;
  const std::string rest = v.substr(i);
  DistUnit u;
  if (rest.empty() || rest == "mi")
    u = DistUnit::MILES;
  else if (rest == "km")
    u = DistUnit::KM;
  else if (rest == "nm")
    u = DistUnit::NM;
  else
    return false;
  n = (uint16_t) std::min<uint32_t>(num, radius_max(u));
  unit = (uint8_t) u;
  return true;
}

Units units_for_zone(int tz_index) { return espn::tz_miles(tz_index) ? Units() : units_metric(); }

espn::AppSet prefs_set(Prefs &p, const std::string &key, const std::string &value) {
  const Prefs was = p;
  if (key == "flyhome") {
    if (value.empty() || value == "none") {
      p.lat_e4 = p.lon_e4 = kNoCoord;
      p.place[0] = '\0';
    } else {
      int32_t lat = 0, lon = 0;
      if (!parse_coords(value, lat, lon))
        return espn::AppSet::REFUSED;
      p.lat_e4 = lat;
      p.lon_e4 = lon;
    }
  } else if (key == "flyplace") {
    copy_field(p.place, sizeof(p.place), clean_place(value));
  } else if (key == "flyradius") {
    uint16_t n = 0;
    uint8_t unit = 0;
    if (!parse_radius(value, n, unit))
      return espn::AppSet::REFUSED;
    p.radius = n;
    p.radius_unit = unit;
  } else if (key == "flyreceiver") {
    const std::string v = value == "none" ? std::string() : value;
    if (v.size() > kReceiverMax)
      return espn::AppSet::REFUSED;
    for (unsigned char c : v)
      if (c <= 0x20 || c >= 0x7F)
        return espn::AppSet::REFUSED;
    copy_field(p.receiver, sizeof(p.receiver), v);
  } else {
    return espn::AppSet::REFUSED;
  }
  return memcmp(&was, &p, sizeof(Prefs)) == 0 ? espn::AppSet::SAME : espn::AppSet::CHANGED;
}

void prefs_state(const Prefs &p, const Units &u, JsonObject out) {
  out["home"] = coord_text(p.lat_e4, p.lon_e4);
  out["place"] = (const char *) p.place;
  out["radius"] = p.radius;
  out["radius_unit"] = dist_unit_key((DistUnit) p.radius_unit);
  out["receiver"] = (const char *) p.receiver;
  out["units"] = u.dist == DistUnit::MILES ? "imperial" : "metric";
}

// ---- The display ---------------------------------------------------------------

const char *const *FlightsApp::hosts() const { return kHosts; }

void FlightsApp::setup() {
  set_json_allocator(espn::detail::json_allocator());
  prefs_clean(this->prefs_);
}

espn::AppSet FlightsApp::set(const std::string &key, const std::string &value) {
  const Prefs was = this->prefs_;
  const espn::AppSet r = prefs_set(this->prefs_, key, value);
  if (r != espn::AppSet::CHANGED)
    return r;
  const Prefs &now = this->prefs_;
  const bool moved = was.lat_e4 != now.lat_e4 || was.lon_e4 != now.lon_e4 || was.radius != now.radius ||
                     was.radius_unit != now.radius_unit || strcmp(was.receiver, now.receiver) != 0;
  if (moved) {
    // A new spot: drop the old planes, read now.
    this->near_ = Nearby();
    this->got_s_ = 0;
    this->fails_ = 0;
    this->card_hex_[0] = '\0';
    for (size_t i = 0; i < kSources; i++)
      this->feed_.read_once[i] = false;
    // A new receiver isn't held to the old one's waits.
    if (strcmp(was.receiver, now.receiver) != 0)
      this->feed_.wait[(size_t) Source::LOCAL].clear();
  }
  this->read_ = Read::NONE;  // the framework drops a read in flight
  return r;
}

void FlightsApp::state(JsonObject out, int64_t now) const {
  prefs_state(this->prefs_, this->units_(), out);
  const bool failing = this->fails_ >= kNoDataFails;
  const Home home = prefs_home(this->prefs_);
  if (!home.valid()) {
    out["status"] = "no_home";
    return;
  }
  // Flightradar24 can't cover this box.
  if (box_wraps(home))
    out["fr24"] = "skip";
  if (this->got_s_ == 0) {
    out["status"] = failing ? "failing" : "waiting";
    return;
  }
  // Not read lately: off screen, or failing.
  out["status"] = failing ? "failing" : now != 0 && now - this->got_s_ > kOldS ? "old" : "ok";
  out["source"] = source_key(this->got_src_);
  const Nearby &n = this->near_;
  out["planes"] = n.tier == Tier::AIR ? n.air : n.tier == Tier::GROUND ? n.ground : n.n;
  if (n.n == 0)
    return;
  // The card's plane: the device page shows its photo
  const Plane &p = n.planes[this->card_ < n.n ? this->card_ : 0];
  out["nearest"] = plane_label(p);
  if (hex_usable(p))
    out["nearest_hex"] = p.hex;
  if (p.reg[0] != '\0')
    out["nearest_reg"] = p.reg;
}

bool FlightsApp::lit(int64_t now) const {
  return prefs_home(this->prefs_).valid() && this->got_s_ != 0 && now - this->got_s_ <= kOldS && this->near_.n > 0;
}

void FlightsApp::shown(bool on) {
  if (on) {
    this->list_ = false;
    this->screen_s_ = 0;
  } else {
    this->photo_room_ = false;
  }
}

uint32_t FlightsApp::rnd_() {
  uint32_t x = this->seed_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return this->seed_ = x;
}

FlightsApp::Next FlightsApp::next_(int64_t now) const {
  Next n;
  const Home home = prefs_home(this->prefs_);
  if (!home.valid())
    return n;
  const uint32_t now_ms = ms_of(now);
  const FeedPick pick = feed_pick(this->feed_, this->prefs_.receiver[0] != '\0', now_ms, !box_wraps(home));
  n.read = Read::POSITIONS;
  n.source = pick.source;
  n.in_ms = pick.in_ms;
  if (this->got_s_ == 0 || n.in_ms == 0)
    return n;
  auto after = [now_ms](const espn::HostWait &w, uint32_t last, uint32_t gap) {
    const uint32_t since = now_ms - last, due = last == 0 || since >= gap ? 0 : gap - since;
    return std::max(due, w.left(now_ms));
  };
  // Routes for planes whose source has none.
  const RouteAsk ask = route_asks(this->near_, this->routes_, (uint32_t) now);
  if (ask.n > 0 && !this->im_wait_.waiting(now_ms)) {
    const uint32_t t = after(this->im_wait_, this->im_last_ms_, kRoutesetGapMs);
    if (t < n.in_ms) {
      n.read = Read::ROUTESET;
      n.in_ms = t;
    }
  } else if (ask.n > 0) {
    const uint32_t t = after(this->db_wait_, this->db_last_ms_, kAdsbdbGapMs);
    if (t < n.in_ms) {
      n.read = Read::ADSBDB;
      n.in_ms = t;
    }
  }
  // Then the card plane's photo
  char key[9];
  if (this->photo_ask_(now, key)) {
    const uint32_t t = after(this->photo_wait_, this->photo_last_ms_, kPhotoGapMs);
    if (t < n.in_ms) {
      n.read = Read::PHOTO;
      n.in_ms = t;
    }
  }
  return n;
}

bool FlightsApp::photo_ask_(int64_t now, char key[9]) const {
  const Nearby &n = this->near_;
  if (!this->photo_room_ || n.n == 0 || this->got_s_ == 0 || now - this->got_s_ > kOldS)
    return false;
  return photo_key(n.planes[this->card_ < n.n ? this->card_ : 0], key) && this->photos_.wants(key, (uint32_t) now);
}

uint32_t FlightsApp::poll_ms(int64_t now) const {
  const Next n = this->next_(now);
  if (n.read == Read::NONE)
    return 60 * 1000;
  // Due now: the panel's two clocks never line up
  if (n.in_ms == 0)
    return 0;
  const uint32_t since = this->last_req_s_ == 0 || now < this->last_req_s_ ? 0 : ms_of(now - this->last_req_s_);
  return since + std::min<uint32_t>(n.in_ms, 24u * 3600 * 1000);
}

void FlightsApp::lost_(uint32_t now_ms) {
  switch (this->read_) {
    case Read::POSITIONS:
      feed_done(this->feed_, this->read_src_, -1, false, 0, this->rnd_(), now_ms);
      if (this->fails_ < 255)
        this->fails_++;
      break;
    case Read::ROUTESET:
      this->im_wait_.fail(false, 0, this->rnd_(), now_ms);
      break;
    case Read::ADSBDB:
      this->db_wait_.fail(false, 0, this->rnd_(), now_ms);
      break;
    case Read::PHOTO:
      this->photo_wait_.fail(false, 0, this->rnd_(), now_ms);
      break;
    default:
      break;
  }
  this->read_ = Read::NONE;
}

bool FlightsApp::request(int64_t now, espn::AppRequest &out) {
  const uint32_t now_ms = ms_of(now);
  this->seed_ ^= now_ms;
  if (this->read_ != Read::NONE)
    this->lost_(now_ms);
  const Next n = this->next_(now);
  if (n.read == Read::NONE || n.in_ms != 0)
    return false;
  out = espn::AppRequest();
  this->read_home_ = prefs_home(this->prefs_);
  switch (n.read) {
    case Read::POSITIONS:
      switch (n.source) {
        case Source::LOCAL:
          out.url = local_url(this->prefs_.receiver, this->read_home_);
          break;
        case Source::ADSBFI:
          out.url = adsbfi_url(this->read_home_);
          break;
        case Source::ADSBLOL:
          out.url = adsblol_url(this->read_home_);
          break;
        default:
          out.url = fr24_url(this->read_home_);
          break;
      }
      feed_started(this->feed_, n.source, now_ms);
      break;
    case Read::ROUTESET:
      this->ask_ = route_asks(this->near_, this->routes_, (uint32_t) now);
      out.url = kRoutesetUrl;
      out.body = routeset_body(this->ask_);
      out.headers.push_back({"Content-Type", "application/json"});
      this->im_last_ms_ = now_ms | 1;
      this->pending_routes_ = this->routes_;
      break;
    case Read::PHOTO:
      this->photo_ask_(now, this->photo_key_);
      out.url = photo_url(this->photo_key_);
      out.headers.push_back({"User-Agent", photo_agent(espn::app_env().version)});
      this->photo_last_ms_ = now_ms | 1;
      this->pending_photo_ = PlanePhoto();
      break;
    default: {
      const RouteAsk ask = route_asks(this->near_, this->routes_, (uint32_t) now);
      memcpy(this->db_callsign_, ask.callsign[0], sizeof(this->db_callsign_));
      out.url = adsbdb_url(this->db_callsign_);
      this->db_last_ms_ = now_ms | 1;
      this->pending_routes_ = this->routes_;
      break;
    }
  }
  this->read_ = n.read;
  this->read_src_ = n.source;
  this->status_ = -1;
  this->retry_ms_ = 0;
  this->last_req_s_ = now;
  return true;
}

bool FlightsApp::parse(espn::AppStream &in, int64_t now) {
  switch (this->read_) {
    case Read::POSITIONS:
      if (this->read_src_ == Source::FR24)
        return parse_fr24_from(in, this->read_home_, now, this->pending_);
      return parse_aircraft_from(in, this->read_home_, this->pending_);
    case Read::ROUTESET:
      return parse_routeset_from(in, this->ask_, this->pending_routes_, (uint32_t) now);
    case Read::ADSBDB:
      return parse_adsbdb_from(in, this->db_callsign_, this->pending_routes_, (uint32_t) now);
    case Read::PHOTO:
      return parse_photo_from(in, this->pending_photo_);
    default:
      return false;
  }
}

void FlightsApp::apply(bool ok, int64_t now) {
  const uint32_t now_ms = ms_of(now);
  const bool refused = espn::is_throttle_status(this->status_);
  switch (this->read_) {
    case Read::POSITIONS:
      feed_done(this->feed_, this->read_src_, ok ? 200 : this->status_, ok, this->retry_ms_, this->rnd_(), now_ms);
      if (ok) {
        this->near_ = this->pending_;
        this->got_s_ = now;
        this->got_src_ = this->read_src_;
        this->fails_ = 0;
        this->card_ = pick_card(this->near_, this->card_hex_);
        this->card_hex_[0] = '\0';
        if (this->near_.n > 0)
          memcpy(this->card_hex_, this->near_.planes[this->card_].hex, sizeof(this->card_hex_));
      } else if (this->fails_ < 255) {
        this->fails_++;
      }
      break;
    case Read::ROUTESET:
      if (ok) {
        this->routes_ = this->pending_routes_;
        this->im_wait_.clear();
      } else {
        this->im_wait_.fail(refused, this->retry_ms_, this->rnd_(), now_ms);
      }
      break;
    case Read::ADSBDB:
      if (ok) {
        this->routes_ = this->pending_routes_;
        this->db_wait_.clear();
      } else if (this->status_ == 404) {
        route_unknown(this->routes_, this->db_callsign_, (uint32_t) now);
        this->db_wait_.clear();
      } else {
        this->db_wait_.fail(refused, this->retry_ms_, this->rnd_(), now_ms);
      }
      break;
    case Read::PHOTO: {
      // An answer but no photo is a miss too
      const bool miss = !ok && (this->status_ == 200 || this->status_ == 400 || this->status_ == 404);
      if (ok || miss) {
        PlanePhoto &p = this->photos_.put(this->photo_key_, (uint32_t) now);
        if (ok && this->pending_photo_.state == PhotoState::KNOWN) {
          memcpy(p.url, this->pending_photo_.url, sizeof(p.url));
          memcpy(p.by, this->pending_photo_.by, sizeof(p.by));
          p.state = PhotoState::KNOWN;
        }
        this->photo_wait_.clear();
      } else {
        this->photo_wait_.fail(refused, this->retry_ms_, this->rnd_(), now_ms);
      }
      break;
    }
    default:
      break;
  }
  this->read_ = Read::NONE;
}

void FlightsApp::frame(int w, int h, int64_t now, espn::AppFrame &out) const {
  FrameOpts o;
  o.w = w;
  o.h = h;
  o.units = this->units_();
  const Home home = prefs_home(this->prefs_);
  if (!home.valid()) {
    put_text(out, note_frame(Note::NO_HOME, o, home.radius_nm));
    return;
  }
  if (this->got_s_ == 0 || now - this->got_s_ > kOldS) {
    put_text(out, note_frame(this->fails_ >= kNoDataFails ? Note::NO_DATA : Note::LOADING, o, home.radius_nm));
    return;
  }
  if (this->near_.n == 0) {
    put_text(out, note_frame(Note::NONE_NEAR, o, home.radius_nm));
    return;
  }
  // The card and the list take turns under 192 wide.
  if (w < 192) {
    if (this->screen_s_ == 0 || now < this->screen_s_) {
      this->screen_s_ = now;
    } else if (now - this->screen_s_ >= (int64_t) (kScreenMs / 1000) - 1) {
      this->list_ = !this->list_;
      this->screen_s_ = now;
    }
  }
  o.card = this->card_;
  o.list = w < 192 && this->list_;
  char key[9];
  if (w >= 128) {
    this->photo_room_ = true;
    if (photo_key(this->near_.planes[this->card_ < this->near_.n ? this->card_ : 0], key))
      o.photo = this->photos_.find(key, (uint32_t) now);
  }
  put_text(out, flight_frame(this->near_, &this->routes_, o, (uint32_t) now));
}

}
