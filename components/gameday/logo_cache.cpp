#include "logo_cache.h"

#include "esphome/components/runtime_image/image_decoder.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
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

LogoCache::Entry *LogoCache::find_(const std::string &url) {
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

LogoCache::Entry *LogoCache::add_(const std::string &url, uint8_t prio, uint32_t now) {
  auto e = std::make_unique<Entry>();
  e->url.assign(url.data(), url.size());
  e->used_ms = now;
  Entry *p = e.get();
  this->entries_.push_back(std::move(e));
  this->queue_(*p, prio, now);
  return p;
}

image::Image *LogoCache::get(const std::string &url) {
  if (url.empty() || this->lock_ == nullptr)
    return nullptr;
  Locked guard(this->lock_);
  uint32_t now = millis();
  Entry *e = this->find_(url);
  if (e == nullptr) {
    this->make_room_(now);
    this->add_(url, ::espn::LOGO_NOW, now);
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

bool LogoCache::waiting(const std::string &url) {
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
  return false;
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
    bool busy = e->state == State::QUEUED || e->state == State::LOADING || e->state == State::DOWNLOADED;
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
    const ::espn::LogoNote note =
        ::espn::logo_learned(!::espn::plain_logo_url(url).empty(), first != url, code, plain_code);
    size_t bytes = body.size();
    {
      Locked guard(this->lock_);
      const uint32_t now = millis();
      ::espn::memo_note(this->memo_, url_key(url), note, now);
      if (ok) {
        job->body = std::move(body);
        job->state = State::DOWNLOADED;
      } else {
        job->state = State::FAILED;
        job->failed_ms = now;
        if (note == ::espn::LogoNote::NONE)
          job->wait_ms = ::espn::kLogoNoneMs;
        else
          ::espn::logo_failed(job->fails, job->wait_ms, throttled, retry_after_ms, random_uint32());
      }
    }
    if (ok) {
      ESP_LOGD(TAG, "Downloaded %s (%u bytes, %u ms)", (plain_code == 200 ? plain : first).c_str(), (unsigned) bytes,
               (unsigned) (millis() - start));
    } else if (note == ::espn::LogoNote::NONE) {
      ESP_LOGI(TAG, "No logo at %s; its badge shows for now", url.c_str());
    } else {
      ESP_LOGW(TAG, "Logo download failed after %u ms: %s", (unsigned) (millis() - start), url.c_str());
    }
    this->downloaded_ = true;
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

// Decodes into the entry itself, so the image object lives in PSRAM with it.
// Safe without the lock: the entry is DOWNLOADED until the caller marks it
// READY, so nothing draws it and nothing drops it.
bool LogoCache::decode_(std::optional<runtime_image::RuntimeImage> &image, const Bytes &body) {
  image.emplace(runtime_image::PNG, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_ALPHA_CHANNEL, nullptr, false,
                LOGO_SIZE, LOGO_SIZE);
  bool ok = image->begin_decode(body.size());
  size_t offset = 0;
  while (ok && offset < body.size()) {
    // feed_data takes a non-const pointer but only reads it.
    int used = image->feed_data(const_cast<uint8_t *>(body.data()) + offset, body.size() - offset);
    if (used < 0) {
      ESP_LOGW(TAG, "Logo decode error: %s", runtime_image::decode_error_to_string(used));
      ok = false;
    }
    if (used <= 0)
      break;
    offset += (size_t) used;
  }
  if (ok)
    ok = image->end_decode();
  if (!ok)
    image.reset();
  return ok;
}

bool LogoCache::loop() {
  if (this->lock_ == nullptr)
    return false;
  if (this->pending_any_.exchange(false))
    this->drain_pending_();
  this->retry_pinned_(millis());
  if (!this->downloaded_.exchange(false))
    return false;
  bool ready = false;
  while (true) {
    Entry *job = nullptr;
    Bytes body;
    {
      Locked guard(this->lock_);
      for (auto &e : this->entries_) {
        if (e->state == State::DOWNLOADED) {
          job = e.get();
          body = std::move(e->body);
          e->body = {};
          break;
        }
      }
    }
    if (job == nullptr)
      return ready;
    // Only the main loop drops entries, so job stays valid without the lock.
    bool ok = this->decode_(job->image, body);
    Locked guard(this->lock_);
    if (ok) {
      job->state = State::READY;
      ::espn::logo_loaded(job->fails, job->wait_ms);
      ready = true;
    } else {
      job->state = State::FAILED;
      job->failed_ms = millis();
      ::espn::logo_failed(job->fails, job->wait_ms, false, 0, random_uint32());
    }
  }
}

}  // namespace esphome::gameday
