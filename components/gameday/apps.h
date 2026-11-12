// Displays an area can show; pure, for host tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <ArduinoJson.h>

#include "espn_parse.h"
#include "health.h"
#include "idle.h"
#include "leagues.h"
#include "panel_text.h"
#include "recap.h"
#include "source_hockeytech.h"
#include "source_mlbstats.h"

namespace espn {

// Saved as a byte: append only, never renumber.
enum class Display : uint8_t { GAMEDAY = 0, F1 = 1, MARKETS = 2, FLIGHTS = 3 };
constexpr int kDisplayCount = 4;
static_assert(kGameSourceCount + kDisplayCount - 1 == kSourceCount, "a Source per display but Game Day");
static_assert(kHealthApps == kDisplayCount - 1, "the health block has a slot per display but Game Day");

// The state document's and the set keys' names.
inline const char *display_key(Display d) {
  switch (d) {
    case Display::F1:
      return "f1";
    case Display::MARKETS:
      return "markets";
    case Display::FLIGHTS:
      return "flights";
    default:
      return "gameday";
  }
}

// What the device page calls it.
inline const char *display_name(Display d) {
  switch (d) {
    case Display::F1:
      return "F1";
    case Display::MARKETS:
      return "Stocks & Crypto";
    case Display::FLIGHTS:
      return "Flight Tracker";
    default:
      return "Game Day";
  }
}

// Bit per display the firmware has; Game Day's always.
constexpr uint8_t kGameDayOnly = 1;
inline bool display_has(uint8_t mask, int d) { return d >= 0 && d < kDisplayCount && ((mask >> d) & 1) != 0; }

// A saved byte the firmware lacks shows Game Day.
inline Display display_effective(uint8_t saved, uint8_t mask) {
  return display_has(mask, saved) ? (Display) saved : Display::GAMEDAY;
}

// Each other display reads and backs off on its own.
inline Source display_source(Display d) {
  return d == Display::GAMEDAY ? Source::ESPN : (Source) (kGameSourceCount + (int) d - 1);
}

// ---- What the main area shows --------------------------------------------------

enum class MainView : uint8_t { GAMEDAY = 0, APP, CLOCK };

// At night the clock, unless the display is busy.
inline MainView main_view(Display d, bool demo, bool night, bool busy) {
  if (d == Display::GAMEDAY || demo)
    return MainView::GAMEDAY;
  return night && !busy ? MainView::CLOCK : MainView::APP;
}

// Off or dim with a display on the main area.
inline bool app_main_dim(uint8_t idle_show, bool clock_valid, bool connected, bool busy, bool area_busy) {
  return idle_show == (uint8_t) IdleShow::OFF && clock_valid && connected && !busy && !area_busy;
}

// ---- The frame a display draws ---------------------------------------------------

constexpr int kAppRows = 6;
// Most points one polyline draws (the views' buffers).
constexpr size_t kAppLinePoints = 48;

struct AppPoint {
  int16_t x{0}, y{0};
  bool operator==(const AppPoint &o) const { return x == o.x && y == o.y; }
};

// Area-local pixels; fewer than two points draws nothing.
struct AppLine {
  std::vector<AppPoint> pts;
  uint32_t color{0xFFFFFF};
};

// The leaderboard's shape: title, status, up to kAppRows rows.
struct AppFrame {
  BoardLines rows;
  std::string message;  // with no rows and no line, the centered words
  std::string ticker;
  std::vector<esphome::gameday::TickerSpan> spans;
  AppLine line;
};

// Rows or a line to draw, else the message.
inline bool app_frame_board(const AppFrame &f) { return !f.rows.name.empty() || f.line.pts.size() >= 2; }

// Keeps the first and last, evenly between.
inline std::vector<AppPoint> app_line_thin(const std::vector<AppPoint> &pts, size_t max) {
  if (max < 2)
    return {};
  if (pts.size() <= max)
    return pts;
  std::vector<AppPoint> out;
  out.reserve(max);
  for (size_t i = 0; i < max; i++)
    out.push_back(pts[i * (pts.size() - 1) / (max - 1)]);
  return out;
}

// Cuts every column to kAppRows, the line to its buffer.
inline void app_frame_trim(AppFrame &f) {
  auto cut = [](auto &v) {
    if (v.size() > (size_t) kAppRows)
      v.resize(kAppRows);
  };
  cut(f.rows.pos);
  cut(f.rows.name);
  cut(f.rows.value);
  cut(f.rows.detail);
  cut(f.rows.value_color);
  cut(f.rows.name_color);
  f.line.pts = app_line_thin(f.line.pts, kAppLinePoints);
}

// Flat x, y pairs clipped to the area.
inline std::vector<int16_t> app_line_xy(const AppLine &line, int w, int h) {
  std::vector<int16_t> out;
  if (line.pts.size() < 2 || w < 1 || h < 1)
    return out;
  const std::vector<AppPoint> pts = app_line_thin(line.pts, kAppLinePoints);
  out.reserve(pts.size() * 2);
  for (const AppPoint &p : pts) {
    out.push_back((int16_t) std::max(0, std::min<int>(p.x, w - 1)));
    out.push_back((int16_t) std::max(0, std::min<int>(p.y, h - 1)));
  }
  return out;
}

struct AppBox {
  int16_t x{0}, y{0}, w{0}, h{0};
};

// Raw points, y up, scaled into box.
inline std::vector<AppPoint> app_line_fit(const std::vector<float> &xs, const std::vector<float> &ys, AppBox box,
                                          bool keep_aspect) {
  std::vector<AppPoint> out;
  const size_t n = std::min(xs.size(), ys.size());
  if (n < 2 || box.w < 2 || box.h < 2)
    return out;
  float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
  for (size_t i = 1; i < n; i++) {
    x0 = std::min(x0, xs[i]);
    x1 = std::max(x1, xs[i]);
    y0 = std::min(y0, ys[i]);
    y1 = std::max(y1, ys[i]);
  }
  const float spanx = x1 - x0, spany = y1 - y0;
  float sx = spanx > 0 ? (float) (box.w - 1) / spanx : 0.0f;
  float sy = spany > 0 ? (float) (box.h - 1) / spany : 0.0f;
  if (keep_aspect) {
    const float s = spanx <= 0 ? sy : spany <= 0 ? sx : std::min(sx, sy);
    sx = sy = s;
  }
  // Centered in what the scale leaves over.
  const float ox = box.x + ((box.w - 1) - spanx * sx) / 2.0f;
  const float oy = box.y + ((box.h - 1) - spany * sy) / 2.0f;
  out.reserve(n);
  for (size_t i = 0; i < n; i++) {
    AppPoint p;
    p.x = (int16_t) std::lround(ox + (xs[i] - x0) * sx);
    p.y = (int16_t) std::lround(oy + (y1 - ys[i]) * sy);
    if (out.empty() || !(out.back() == p))
      out.push_back(p);
  }
  return app_line_thin(out, kAppLinePoints);
}

// A price series left to right, highest at the top.
inline std::vector<AppPoint> app_sparkline(const std::vector<float> &values, AppBox box) {
  std::vector<float> xs(values.size());
  for (size_t i = 0; i < xs.size(); i++)
    xs[i] = (float) i;
  return app_line_fit(xs, values, box, false);
}

// ---- Reads ----------------------------------------------------------------------

struct AppHeader {
  std::string name, value;
};

// A User-Agent header replaces the panel's curl/8.0.
struct AppRequest {
  std::string url;
  std::string body;  // POST when set
  std::vector<AppHeader> headers;
};

// One response body, read once on the fetch task.
class AppStream {
 public:
  virtual ~AppStream() = default;
  virtual int read() = 0;
  virtual size_t readBytes(char *dst, size_t n) = 0;
};

// A whole body in memory, for host tests.
class AppTextStream final : public AppStream {
 public:
  explicit AppTextStream(std::string text) : text_(std::move(text)) {}
  int read() override { return this->at_ < this->text_.size() ? (unsigned char) this->text_[this->at_++] : -1; }
  size_t readBytes(char *dst, size_t n) override {
    const size_t take = std::min(n, this->text_.size() - this->at_);
    memcpy(dst, this->text_.data() + this->at_, take);
    this->at_ += take;
    return take;
  }

 private:
  std::string text_;
  size_t at_{0};
};

// "https://api.example.com:443/v1" to "api.example.com"
inline std::string url_host(const std::string &url) {
  const size_t scheme = url.find("://");
  const size_t from = scheme == std::string::npos ? 0 : scheme + 3;
  size_t end = url.find_first_of(":/?#", from);
  if (end == std::string::npos)
    end = url.size();
  std::string host = url.substr(from, end - from);
  for (char &c : host)
    if (c >= 'A' && c <= 'Z')
      c = (char) (c - 'A' + 'a');
  return host;
}

// The host itself, or one under it.
inline bool host_is(const std::string &host, const char *name) {
  const size_t n = strlen(name);
  if (n == 0 || host.size() < n || host.compare(host.size() - n, n, name) != 0)
    return false;
  return host.size() == n || host[host.size() - n - 1] == '.';
}

struct AppHost {
  const char *host{nullptr};
  Source source{Source::ESPN};
};

// Which host's accounting a read goes to.
inline Source url_source(const std::string &url, const AppHost *hosts, size_t n) {
  if (is_mlb_url(url))
    return Source::MLB_STATS;
  if (is_ht_url(url))
    return Source::HOCKEYTECH;
  const std::string host = url_host(url);
  for (size_t i = 0; i < n; i++)
    if (hosts[i].host != nullptr && host_is(host, hosts[i].host))
      return hosts[i].source;
  return Source::ESPN;
}

// Never read, or its wait since the last start passed.
inline bool app_due(uint32_t now, uint32_t last_ms, uint32_t wait_ms) {
  return last_ms == 0 || now - last_ms >= wait_ms;
}

// How long each display's reads take, as BoxTimes does.
struct AppTimes {
  static constexpr uint32_t kFirstMs = 5000, kMarginMs = 1000, kMaxMs = 60000;
  uint16_t ms[kDisplayCount]{};
  uint8_t skipped{0};
  uint32_t expect(Display d) const {
    const uint16_t m = this->ms[(int) d];
    return m != 0 ? m + kMarginMs : kFirstMs;
  }
  void skip(Display d) { this->skipped |= (uint8_t) (1u << (int) d); }
  void started(Display d) { this->skipped &= (uint8_t) ~(1u << (int) d); }
  // Failures slower than expected count too.
  void took(Display d, uint32_t took_ms, bool ok) {
    const uint16_t t = (uint16_t) std::min(took_ms, kMaxMs);
    if (ok || t > this->expect(d))
      this->ms[(int) d] = t;
  }
  // Skipped kinds shrink so they aren't starved.
  void cycle() {
    for (int k = 0; k < kDisplayCount; k++)
      if ((this->skipped >> k) & 1)
        this->ms[k] -= this->ms[k] / 8;
    this->skipped = 0;
  }
};

// ---- A display ------------------------------------------------------------------

enum class AppSet : uint8_t { REFUSED = 0, SAME, CHANGED };

// Main loop only, except parse (the fetch task).
class App {
 public:
  virtual ~App() = default;
  virtual Display id() const = 0;
  // Hosts its reads go to, nullptr-ended.
  virtual const char *const *hosts() const = 0;

  // A fixed-size blob, loaded before setup; none by default.
  virtual void *prefs() { return nullptr; }
  virtual size_t prefs_size() const { return 0; }
  virtual void setup() {}

  // Its own set keys, nullptr-ended, taken after kSetKeys.
  virtual const char *const *set_keys() const { return nullptr; }
  // CHANGED saves prefs and drops a read in flight.
  virtual AppSet set(const std::string & /*key*/, const std::string & /*value*/) { return AppSet::REFUSED; }
  // Its object in the state document, under display_key.
  virtual void state(JsonObject /*out*/) const {}

  // On screen in some area now, or not.
  virtual void shown(bool /*on*/) {}
  // Keeps it up through night hours and Off or dim.
  virtual bool busy(int64_t /*now*/) const { return false; }

  // From one read's start to the next; 0 is now.
  virtual uint32_t poll_ms(int64_t now) const = 0;
  // The next read; false for none now.
  virtual bool request(int64_t now, AppRequest &out) = 0;
  // Fetch task: into its own result, in PSRAM.
  virtual bool parse(AppStream &in, int64_t now) = 0;
  // After a read under the current settings.
  virtual void apply(bool ok, int64_t now) = 0;
  // A w by h area's frame.
  virtual void frame(int w, int h, int64_t now, AppFrame &out) const = 0;
  // Redraws this often on its own; 0 after reads only.
  virtual uint32_t redraw_ms() const { return 0; }
};

// The displays this firmware has, by Display.
struct AppSlots {
  App *app[kDisplayCount]{};
  App *get(Display d) const { return (int) d < kDisplayCount ? this->app[(int) d] : nullptr; }
  uint8_t mask() const {
    uint8_t m = kGameDayOnly;
    for (int d = 1; d < kDisplayCount; d++)
      if (this->app[d] != nullptr)
        m |= (uint8_t) (1u << d);
    return m;
  }
};

inline AppSlots &app_slots() {
  static AppSlots s;
  return s;
}

// False for Game Day's slot or one taken.
inline bool app_register(AppSlots &slots, App *a) {
  if (a == nullptr || a->id() == Display::GAMEDAY || (int) a->id() >= kDisplayCount ||
      slots.app[(int) a->id()] != nullptr)
    return false;
  slots.app[(int) a->id()] = a;
  return true;
}

// Every display's hosts, for url_source; returns the count.
inline size_t app_hosts(const AppSlots &slots, AppHost *out, size_t max) {
  size_t n = 0;
  for (int d = 1; d < kDisplayCount; d++) {
    const App *a = slots.app[d];
    const char *const *h = a != nullptr ? a->hosts() : nullptr;
    for (; h != nullptr && *h != nullptr && n < max; h++)
      out[n++] = AppHost{*h, display_source((Display) d)};
  }
  return n;
}

// The display claiming a set key; GAMEDAY for none.
inline Display app_for_key(const AppSlots &slots, const std::string &key) {
  for (int d = 1; d < kDisplayCount; d++) {
    const App *a = slots.app[d];
    for (const char *const *k = a != nullptr ? a->set_keys() : nullptr; k != nullptr && *k != nullptr; k++)
      if (key == *k)
        return (Display) d;
  }
  return Display::GAMEDAY;
}

// One display object in the PSRAM heap, never freed.
template<class T, class... Args> T *app_new(Args &&...args) {
  const RecapHeap &h = recap_heap();
  void *p = h.alloc != nullptr ? h.alloc(sizeof(T)) : ::operator new(sizeof(T));
  return p != nullptr ? new (p) T(std::forward<Args>(args)...) : nullptr;
}

template<class T> using AppVector = std::vector<T, RecapAllocator<T>>;
using AppString = std::basic_string<char, std::char_traits<char>, RecapAllocator<char>>;

// Each display's object, created at boot (app_list.cpp).
void register_apps(AppSlots &slots);

}  // namespace espn
