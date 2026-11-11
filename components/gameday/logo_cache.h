#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/runtime_image/runtime_image.h"
#include "logo_lru.h"

namespace esphome::gameday {

// PSRAM first, internal RAM only if PSRAM is full. Aborts on failure, as
// operator new does with exceptions off.
inline void *psram_alloc(size_t size) {
  void *p = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
  if (p == nullptr)
    abort();
  return p;
}

template<class T> struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template<class U> PsramAllocator(const PsramAllocator<U> &) {}
  T *allocate(size_t n) { return static_cast<T *>(psram_alloc(n * sizeof(T))); }
  void deallocate(T *p, size_t) { heap_caps_free(p); }
  template<class U> bool operator==(const PsramAllocator<U> &) const { return true; }
  template<class U> bool operator!=(const PsramAllocator<U> &) const { return false; }
};

// Team logos, downloaded and decoded once, then kept in PSRAM until the
// panel restarts. Downloads run on their own task so a slow logo server can
// only delay a logo, never the main loop. Decoding stays on the main loop
// because ESPHome's PNG decoder feeds the main loop's watchdog.
//
// Everything a logo allocates goes to PSRAM, not just its pixels. Plain new
// and malloc only use internal RAM in this build, so the PNG download and
// each entry's small objects used to land there, and a full cache broke
// internal RAM into pieces too small for the fetch task's 16 KB stack.
class LogoCache {
 public:
  void set_http(http_request::HttpRequestComponent *http) { this->http_ = http; }
  void setup();
  // Any task. Starts downloading a logo the board is about to ask for.
  void prefetch(const std::string &url);

  // The rest is main loop only.

  // Like prefetch, but may drop an old logo
  void want(const std::string &url);
  // The decoded logo, or nullptr while it is still missing; a miss starts a download.
  image::Image *get(const std::string &url);
  // True while the logo is still on its way. A miss starts it, as get() does;
  // a failed logo is not on its way.
  bool waiting(const std::string &url);
  // The two URLs about to be shown. They and the two before them are never
  // dropped, so nothing LVGL may still point at is freed.
  void pin(const std::string &team, const std::string &opponent) { this->pin_at_(0, team, opponent); }
  void pin_a2(const std::string &team, const std::string &opponent) { this->pin_at_(4, team, opponent); }
  bool busy() const { return this->task_running_; }
  // Decodes finished downloads and retries failed logos that are on screen.
  // True when at least one logo became ready.
  bool loop();
  // Logo bytes downloaded since boot, any task.
  uint32_t bytes_downloaded() const { return this->bytes_downloaded_.load(); }

 protected:
  using Bytes = std::vector<uint8_t, PsramAllocator<uint8_t>>;
  using Url = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

  enum class State : uint8_t { QUEUED, LOADING, DOWNLOADED, READY, FAILED };
  struct Entry {
    static void *operator new(size_t size) { return psram_alloc(size); }
    static void operator delete(void *p) { heap_caps_free(p); }

    Url url;
    Bytes body;    // the PNG, from the task to the main loop
    Bytes pixels;  // the decoded logo, RGB565 then alpha
    std::optional<image::Image> image;
    State state{State::QUEUED};
    uint8_t prio{::espn::LOGO_NOW};
    uint32_t seq{0};   // queue order within a priority
    uint8_t fails{0};  // failures in a row, for the wait
    uint32_t used_ms{0};
    uint32_t failed_ms{0};
    uint32_t wait_ms{0};  // how long after failed_ms to ask again
  };

  static void task_(void *arg);
  void run_();
  // code: the HTTP status, 0 for no answer
  bool download_(const std::string &url, Bytes &body, int &code, bool &throttled, uint32_t &retry_after_ms);
  bool decode_(Entry &e, const Bytes &body);
  void make_room_(uint32_t now);  // now: millis()
  void start_task_();
  bool pinned_url_(std::string_view url) const;
  void pin_at_(size_t at, const std::string &team, const std::string &opponent);
  void retry_pinned_(uint32_t now);
  void drain_pending_();
  // These three: caller holds the lock
  Entry *find_(const std::string &url);
  Entry *add_(const std::string &url, uint8_t prio, uint32_t now);
  void queue_(Entry &e, uint8_t prio, uint32_t now);

  http_request::HttpRequestComponent *http_{nullptr};
  // One PNG decoder (~44 KB) shared by every logo
  std::optional<runtime_image::RuntimeImage> decoder_;
  // Guards entries_, memo_, pending_ and every Entry's state and body.
  SemaphoreHandle_t lock_{nullptr};
  std::vector<std::unique_ptr<Entry>, PsramAllocator<std::unique_ptr<Entry>>> entries_;
  std::vector<::espn::LogoSlot, PsramAllocator<::espn::LogoSlot>> slots_;  // scratch space for make_room_
  std::vector<::espn::LogoMemo, PsramAllocator<::espn::LogoMemo>> memo_;
  std::vector<Url, PsramAllocator<Url>> pending_;  // prefetches for the main loop
  uint32_t seq_{0};
  std::string pinned_[8];  // board pair, previous pair, then second area's two
  uint32_t retry_check_ms_{0};
  bool task_running_{false};
  std::atomic<bool> downloaded_{false};
  std::atomic<bool> pending_any_{false};
  std::atomic<uint32_t> bytes_downloaded_{0};
};

}  // namespace esphome::gameday
