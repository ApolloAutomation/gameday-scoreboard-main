#include "logo_cache.h"

#include <algorithm>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "logo_decode.h"
#include "logo_icon.h"
#include "logo_lru.h"
#include "psram_tasks.h"

namespace esphome::gameday {

static const char *const TAG = "gameday.logo";
static const char *const USER_AGENT = ::espn::kUserAgent;  // the same as every ESPN read (poll_timing.h)
// Rotating live games plus today's scores, ~3 KB each
static const size_t MAX_LOGOS = 128;
static const size_t MAX_PENDING = 8;
// ESPN's PNGs are a few KB; anything far bigger is not a logo.
static const size_t MAX_BODY = 128 * 1024;
static const int LOGO_SIZE = 32;

class Locked {
 public:
  explicit Locked(SemaphoreHandle_t lock) : lock_(lock) { xSemaphoreTake(lock_, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(lock_); }

 private:
  SemaphoreHandle_t lock_;
};

template<class S> static uint32_t url_key(const S &url) { return ::espn::logo_key(url.data(), url.size()); }

void LogoCache::setup() {
  this->lock_ = xSemaphoreCreateMutex();
  this->entries_.reserve(MAX_LOGOS);
  this->slots_.reserve(MAX_LOGOS);
  this->memo_.reserve(::espn::kLogoMemoCap);
  this->pending_.reserve(MAX_PENDING);
}

LogoCache::Entry *LogoCache::find_(std::string_view url) {
  for (auto &e : this->entries_) {
    if (std::string_view(e->url) == url)
      return e.get();
  }
  return nullptr;
}

// A logo known missing waits out its memo instead
void LogoCache::queue_(Entry &e, uint8_t prio, uint32_t now) {
  const uint32_t left = ::espn::memo_none_left(this->memo_, url_key(e.url), now);
  if (left > 0) {
    e.state = State::FAILED;
    e.failed_ms = now;
    e.wait_ms = left;
    return;
  }
  e.state = State::QUEUED;
  e.prio = prio;
  e.seq = ++this->seq_;
  this->start_task_();
}

LogoCache::Entry *LogoCache::add_(std::string_view url, uint8_t prio, uint32_t now) {
  auto e = std::make_unique<Entry>();
  e->url.assign(url.data(), url.size());
  e->used_ms = now;
  Entry *p = e.get();
  this->entries_.push_back(std::move(e));
  this->queue_(*p, prio, now);
  return p;
}

image::Image *LogoCache::get(std::string_view url, uint8_t icon, bool mark) {
  if (url.empty() || this->lock_ == nullptr)
    return nullptr;
  Locked guard(this->lock_);
  uint32_t now = millis();
  Entry *e = this->find_(url);
  if (e == nullptr) {
    this->make_room_(now);
    Entry *n = this->add_(url, ::espn::LOGO_NOW, now);
    n->icon = icon;
    n->mark = mark;
    return nullptr;
  }
  e->used_ms = now;
  if (e->state == State::READY)
    return &*e->image;
  if (e->state == State::QUEUED)
    e->prio = ::espn::LOGO_NOW;  // a card needs it now
  if (e->state == State::FAILED && ::espn::logo_retry_due(e->failed_ms, e->wait_ms, now))
    this->queue_(*e, ::espn::LOGO_NOW, now);
  return nullptr;
}

void LogoCache::want(const std::string &url) {
  if (url.empty() || this->lock_ == nullptr)
    return;
  Locked guard(this->lock_);
  uint32_t now = millis();
  Entry *e = this->find_(url);
  if (e == nullptr) {
    this->make_room_(now);
    this->add_(url, ::espn::LOGO_NEXT, now);
    return;
  }
  e->used_ms = now;
  if (e->state == State::FAILED && ::espn::logo_retry_due(e->failed_ms, e->wait_ms, now))
    this->queue_(*e, ::espn::LOGO_NEXT, now);
}

bool LogoCache::ready(std::string_view url) {
  if (url.empty() || this->lock_ == nullptr)
    return false;
  Locked guard(this->lock_);
  Entry *e = this->find_(url);
  return e != nullptr && e->state == State::READY;
}

bool LogoCache::waiting(std::string_view url) {
  if (url.empty() || this->lock_ == nullptr || this->get(url) != nullptr)
    return false;
  Locked guard(this->lock_);
  Entry *e = this->find_(url);
  return e != nullptr && e->state != State::FAILED;
}

// Never evicts; when full, the main loop's want() does
void LogoCache::prefetch(const std::string &url) {
  if (url.empty() || this->lock_ == nullptr)
    return;
  Locked guard(this->lock_);
  if (this->find_(url) != nullptr)
    return;
  if (this->entries_.size() < MAX_LOGOS) {
    this->add_(url, ::espn::LOGO_NEXT, millis());
    return;
  }
  for (const auto &p : this->pending_) {
    if (std::string_view(p) == url)
      return;
  }
  if (this->pending_.size() < MAX_PENDING) {
    this->pending_.emplace_back(url.data(), url.size());
    this->pending_any_ = true;
  }
}

void LogoCache::drain_pending_() {
  while (true) {
    std::string url;
    {
      Locked guard(this->lock_);
      if (this->pending_.empty())
        return;
      url.assign(this->pending_.front().data(), this->pending_.front().size());
      this->pending_.erase(this->pending_.begin());
    }
    this->want(url);
  }
}

void LogoCache::pin_at_(size_t at, const std::string &team, const std::string &opponent) {
  std::string *p = this->pinned_ + at;
  if (this->lock_ == nullptr || (team == p[0] && opponent == p[1]))
    return;
  Locked guard(this->lock_);
  p[2] = std::move(p[0]);
  p[3] = std::move(p[1]);
  p[0] = team;
  p[1] = opponent;
}

bool LogoCache::pinned_url_(std::string_view url) const {
  for (const auto &p : this->pinned_) {
    if (!p.empty() && p == url)
      return true;
  }
  if (this->icon_pin_n_[0] == 0 && this->icon_pin_n_[1] == 0)
    return false;
  const uint32_t key = url_key(url);
  for (int s = 0; s < 2; s++)
    for (uint8_t i = 0; i < this->icon_pin_n_[s]; i++)
      if (this->icon_pins_[s][i] == key)
        return true;
  return false;
}

void LogoCache::pin_icons(const uint32_t *keys, size_t n) {
  n = std::min(n, ICON_PINS);
  if (this->lock_ == nullptr ||
      (n == this->icon_pin_n_[0] && std::equal(keys, keys + n, this->icon_pins_[0])))
    return;
  Locked guard(this->lock_);
  std::copy(this->icon_pins_[0], this->icon_pins_[0] + ICON_PINS, this->icon_pins_[1]);
  this->icon_pin_n_[1] = this->icon_pin_n_[0];
  std::copy(keys, keys + n, this->icon_pins_[0]);
  this->icon_pin_n_[0] = (uint8_t) n;
}

// Main loop. A failed logo still on screen is asked for again on its own, so
// it does not wait for the next board update, which can be hours away.
void LogoCache::retry_pinned_(uint32_t now) {
  if (now - this->retry_check_ms_ < 1000)
    return;
  this->retry_check_ms_ = now;
  Locked guard(this->lock_);
  for (auto &e : this->entries_) {
    if (e->state == State::FAILED && this->pinned_url_(e->url) &&
        ::espn::logo_retry_due(e->failed_ms, e->wait_ms, now))
      this->queue_(*e, ::espn::LOGO_NOW, now);
  }
}

// Caller holds the lock. Drops the least recently used logo that is idle and
// off screen, so the image it frees is not one LVGL is drawing.
void LogoCache::make_room_(uint32_t now) {
  if (this->entries_.size() < MAX_LOGOS)
    return;
  this->slots_.clear();
  for (auto &e : this->entries_) {
    bool busy = e->state == State::QUEUED || e->state == State::LOADING;
    bool pinned = this->pinned_url_(e->url);
    this->slots_.push_back({e->used_ms, busy, pinned});
  }
  int i = ::espn::pick_evict_in(this->slots_, now);
  if (i >= 0)
    this->entries_.erase(this->entries_.begin() + i);
}

// Caller holds the lock.
void LogoCache::start_task_() {
  if (this->task_running_)
    return;
  this->task_running_ = true;
  // 16 KB: the TLS handshake needs it.
  if (!PsramTasks::start(&LogoCache::task_, "gameday_logo", 16384, this)) {
    ESP_LOGW(TAG, "Could not start the logo task (largest free internal block %u bytes)",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    this->task_running_ = false;
  }
}

void LogoCache::task_(void *arg) {
  static_cast<LogoCache *>(arg)->run_();
  PsramTasks::park();
}

// Logo task: works through queued logos one at a time, then exits. The task
// only touches an entry while it is LOADING, and nothing drops a busy entry.
void LogoCache::run_() {
  while (true) {
    Entry *job = nullptr;
    std::string url, first;
    uint8_t icon = 0;
    bool mark = false;
    {
      Locked guard(this->lock_);
      for (auto &e : this->entries_) {
        if (e->state == State::QUEUED &&
            (job == nullptr || ::espn::logo_job_before(e->prio, e->seq, job->prio, job->seq)))
          job = e.get();
      }
      if (job == nullptr) {
        this->task_running_ = false;  // under the lock, so get() starts a new task for anything queued after this
        return;
      }
      job->state = State::LOADING;
      icon = job->icon;
      mark = job->mark;
      url.assign(job->url.data(), job->url.size());
      first = ::espn::logo_first_url(url, ::espn::memo_get(this->memo_, url_key(url), millis()));
    }
    Bytes body;
    bool throttled = false;
    uint32_t retry_after_ms = 0;
    int code = 0, plain_code = 0;
    uint32_t start = millis();
    bool ok = this->download_(first, body, code, throttled, retry_after_ms);
    // Kept under the dark URL, so lookups still find it
    const std::string plain = first == url ? ::espn::plain_logo_url(url) : std::string();
    if (!ok && code == 404 && !plain.empty()) {
      ESP_LOGD(TAG, "No dark logo, using %s", plain.c_str());
      ok = this->download_(plain, body, plain_code, throttled, retry_after_ms);
    }
    ::espn::LogoNote note =
        ::espn::logo_learned(!::espn::plain_logo_url(url).empty(), first != url, code, plain_code);
    // Google's stand-in for an airline it lacks
    if (ok && ::espn::logo_placeholder(first.c_str(), body.data(), body.size())) {
      ok = false;
      note = ::espn::LogoNote::NONE;
    }
    if (ok) {
      ESP_LOGD(TAG, "Downloaded %s (%u bytes, %u ms)", (plain_code == 200 ? plain : first).c_str(),
               (unsigned) body.size(), (unsigned) (millis() - start));
    }
    Decoded img;
    const uint32_t decode_start = millis();
    const int decoded = ok ? this->decode_(body, icon, mark, img) : 0;
    const uint32_t decode_ms = millis() - decode_start;
    // Too thin or faint: asked again in hours
    if (decoded < 0)
      note = ::espn::LogoNote::NONE;
    {
      Locked guard(this->lock_);
      const uint32_t now = millis();
      ::espn::memo_note(this->memo_, url_key(url), note, now);
      if (decoded > 0) {
        job->pixels = std::move(img.pixels);
        job->image.emplace(job->pixels.data(), img.w, img.h, image::IMAGE_TYPE_RGB565,
                           image::TRANSPARENCY_ALPHA_CHANNEL);
        job->state = State::READY;
        ::espn::logo_loaded(job->fails, job->wait_ms);
        this->loaded_ = true;
      } else {
        job->state = State::FAILED;
        job->failed_ms = now;
        if (note == ::espn::LogoNote::NONE)
          job->wait_ms = ::espn::kLogoNoneMs;
        else
          ::espn::logo_failed(job->fails, job->wait_ms, throttled, retry_after_ms, random_uint32());
        this->failed_ = true;
      }
    }
    if (decoded > 0) {
      ESP_LOGD(TAG, "Decoded %ux%u logo to %dx%d in %u ms", (unsigned) img.src_w, (unsigned) img.src_h, img.w, img.h,
               (unsigned) decode_ms);
    } else if (decoded < 0) {
      ESP_LOGI(TAG, "Logo unreadable at %u px: %s", (unsigned) icon, url.c_str());
    } else if (!ok && note == ::espn::LogoNote::NONE) {
      ESP_LOGI(TAG, "No logo at %s; its badge shows for now", url.c_str());
    } else if (!ok) {
      ESP_LOGW(TAG, "Logo download failed after %u ms: %s", (unsigned) (millis() - start), url.c_str());
    }
  }
}

// On a refusal (403 or 429) sets throttled, and retry_after_ms when the reply
// carried a plain Retry-After.
bool LogoCache::download_(const std::string &url, Bytes &body, int &code, bool &throttled,
                          uint32_t &retry_after_ms) {
  std::vector<http_request::Header> headers = {
      {"User-Agent", USER_AGENT},
      {"Accept", "image/png,*/*;q=0.8"},
  };
  code = 0;
  auto container = this->http_->get(url, headers, {"retry-after"});
  if (container == nullptr)
    return false;
  code = container->status_code;
  if (code != 200) {
    throttled = ::espn::is_throttle_status(code);
    if (throttled)
      retry_after_ms = ::espn::parse_retry_after_ms(container->get_response_header("retry-after"));
    container->end();
    if (code != 404 || ::espn::plain_logo_url(url).empty())
      ESP_LOGW(TAG, "HTTP %d for %s", code, url.c_str());
    return false;
  }
  if (container->content_length > 0 && container->content_length <= MAX_BODY)
    body.reserve(container->content_length);
  uint8_t buf[1024];
  while (body.size() < MAX_BODY) {
    int n = container->read(buf, sizeof(buf));
    if (n <= 0)
      break;
    body.insert(body.end(), buf, buf + n);
  }
  container->end();
  this->bytes_downloaded_ += body.size();
  if (body.empty() || (container->content_length > 0 && body.size() < container->content_length))
    return false;
  return true;
}

namespace {

// One decode, for pngle's callbacks
struct PngJob {
  ::espn::LogoCanvas canvas;
  std::vector<uint8_t, PsramAllocator<uint8_t>> *pixels{nullptr};
  uint32_t px{0};
  uint32_t rest_ms{0};

  // Lets idle tasks and the main loop in
  void rest() {
    if (::espn::logo_rest_due(this->rest_ms, millis())) {
      vTaskDelay(1);
      this->rest_ms = millis();
    }
  }
};

void png_init(pngle_t *p, uint32_t w, uint32_t h) {
  auto *job = static_cast<PngJob *>(pngle_get_user_data(p));
  if (!::espn::logo_size_ok(w, h))
    return;
  int fw = 0, fh = 0;
  ::espn::logo_fit(w, h, LOGO_SIZE, fw, fh);
  job->pixels->assign(::espn::logo_icon_bytes(fw, fh), 0);
  job->canvas = ::espn::LogoCanvas{job->pixels->data(), w, h, fw, fh};
}

void png_draw(pngle_t *p, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  auto *job = static_cast<PngJob *>(pngle_get_user_data(p));
  job->canvas.paint(x, y, w, h, rgba);
  if ((++job->px & 1023) == 0)
    job->rest();
}

}  // namespace

// pngle itself: runtime_image's decoder feeds the loop's watchdog.
int LogoCache::decode_(const Bytes &body, uint8_t icon, bool mark, Decoded &out) {
  uint32_t w = 0, h = 0;
  if (::espn::png_size(body.data(), body.size(), w, h) && !::espn::logo_size_ok(w, h)) {
    ESP_LOGW(TAG, "Logo too large to decode (%ux%u)", (unsigned) w, (unsigned) h);
    return 0;
  }
  if (this->png_ == nullptr) {
    void *p = heap_caps_malloc_prefer(PNGLE_T_SIZE, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
    if (p == nullptr) {
      ESP_LOGW(TAG, "No room for the PNG decoder");
      return 0;
    }
    memset(p, 0, PNGLE_T_SIZE);
    this->png_ = static_cast<pngle_t *>(p);
    pngle_reset(this->png_);
    pngle_set_init_callback(this->png_, png_init);
    pngle_set_draw_callback(this->png_, png_draw);
  }
  PngJob job;
  job.pixels = &out.pixels;
  job.rest_ms = millis();
  pngle_set_user_data(this->png_, &job);
  bool ok = true;
  size_t offset = 0;
  while (offset < body.size()) {
    int used = pngle_feed(this->png_, body.data() + offset, std::min<size_t>(body.size() - offset, 4096));
    if (used < 0) {
      ESP_LOGW(TAG, "Logo decode error: %s", pngle_error(this->png_));
      ok = false;
    }
    if (used <= 0)
      break;
    offset += (size_t) used;
    job.rest();
  }
  // Frees its row buffers
  pngle_reset(this->png_);
  if (!ok)
    return 0;
  if (job.canvas.px == nullptr) {
    ESP_LOGW(TAG, "Logo is not a usable PNG");
    return 0;
  }
  out.src_w = job.canvas.w;
  out.src_h = job.canvas.h;
  out.w = job.canvas.fw;
  out.h = job.canvas.fh;
  if (icon > 0) {
    // Squared so a wide logo keeps its shape
    const int side = std::max(out.w, out.h);
    Bytes square(::espn::logo_icon_bytes(side, side));
    ::espn::logo_icon_square(job.canvas.px, out.w, out.h, square.data());
    if (mark) {
      std::vector<uint16_t, PsramAllocator<uint16_t>> stack;
      ::espn::logo_icon_knockout(square.data(), side, side, stack);
    }
    const std::vector<uint8_t> small = ::espn::logo_icon_shrink(square.data(), side, side, icon);
    out.pixels = Bytes(small.begin(), small.end());
    ::espn::logo_icon_lift(out.pixels.data(), icon, icon, mark);
    out.w = out.h = icon;
    if (mark && !::espn::logo_icon_usable(out.pixels.data(), icon)) {
      out.pixels = Bytes();
      return -1;
    }
  }
  return 1;
}

bool LogoCache::loop(bool &failed) {
  failed = false;
  if (this->lock_ == nullptr)
    return false;
  if (this->pending_any_.exchange(false))
    this->drain_pending_();
  this->retry_pinned_(millis());
  failed = this->failed_.exchange(false);
  return this->loaded_.exchange(false);
}

}  // namespace esphome::gameday
