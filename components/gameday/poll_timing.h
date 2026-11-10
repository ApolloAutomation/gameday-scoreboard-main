// How often the panel reads ESPN and how it backs off. Pure, with no ESPHome
// includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <string>


namespace espn {

// The User-Agent on every request to ESPN and its logo server. ESPN's edge
// returns 403 to most unfamiliar agents, ESPHome's own among them, but
// accepts curl.
static constexpr const char *kUserAgent = "curl/8.0";

// ---- Backoff ------------------------------------------------------------------

// After an ordinary failure (timeout, 5xx, a parse that came up empty) the
// wait doubles from 30 s up to 10 min. After ESPN refuses the panel (403 or
// 429) it doubles from 5 min up to an hour, so a block or a rate limit never
// turns into every panel in the fleet hammering it.
static const uint32_t kBackoffBaseMs = 30 * 1000;
static const uint32_t kBackoffCapMs = 10 * 60 * 1000;
static const uint32_t kThrottleBaseMs = 5 * 60 * 1000;
static const uint32_t kThrottleCapMs = 60 * 60 * 1000;

inline bool is_throttle_status(int status) { return status == 403 || status == 429; }

// Retry-After in seconds, as milliseconds. 0 when it is missing, not a plain
// number of seconds (the HTTP-date form is ignored) or absurd.
inline uint32_t parse_retry_after_ms(const std::string &value) {
  if (value.empty() || value.size() > 6)
    return 0;
  uint32_t s = 0;
  for (char c : value) {
    if (c < '0' || c > '9')
      return 0;
    s = s * 10 + (uint32_t) (c - '0');
  }
  return s * 1000;
}

// The wait before the next read after `fails` failures in a row (1 for the
// first). `rnd` is any random number. The result lands somewhere in the upper
// half of the doubled delay, so panels that failed together (an ESPN outage,
// a router reboot) spread out instead of coming back in one wave. A
// Retry-After longer than that is honored up to the cap, with up to a quarter
// on top for the same reason.
inline uint32_t backoff_ms(uint32_t fails, bool throttled, uint32_t retry_after_ms, uint32_t rnd) {
  const uint32_t base = throttled ? kThrottleBaseMs : kBackoffBaseMs;
  const uint32_t cap = throttled ? kThrottleCapMs : kBackoffCapMs;
  uint32_t d = base;
  for (uint32_t i = 1; i < fails && d < cap; i++)
    d *= 2;
  if (d > cap)
    d = cap;
  uint32_t wait = d / 2 + rnd % (d / 2 + 1);
  if (throttled && retry_after_ms > wait) {
    uint32_t ra = retry_after_ms > cap ? cap : retry_after_ms;
    wait = ra + rnd % (ra / 4 + 1);
  }
  return wait;
}

// The run of failed reads since ESPN last answered. Every fetch path shares
// the component's one run, so a failure anywhere lengthens the next wait.
struct Backoff {
  uint8_t streak{0};
  bool throttled{false};  // ESPN said 403 or 429 since the last good read

  // A read failed: start the next, longer wait and return it. A refusal keeps
  // the longer schedule until a good read clears it.
  uint32_t fail(bool refused, uint32_t retry_after_ms, uint32_t rnd) {
    if (refused)
      this->throttled = true;
    if (this->streak < 255)
      this->streak++;
    return backoff_ms(this->streak, this->throttled, retry_after_ms, rnd);
  }
  void clear() {
    this->streak = 0;
    this->throttled = false;
  }
};

// Per host, so one host's refusal never holds back another.
struct HostWait {
  Backoff run;
  uint32_t since_ms{0};
  uint32_t wait_ms{0};  // 0: no wait running

  uint32_t fail(bool refused, uint32_t retry_after_ms, uint32_t rnd, uint32_t now) {
    this->wait_ms = this->run.fail(refused, retry_after_ms, rnd);
    this->since_ms = now;
    return this->wait_ms;
  }
  void clear() {
    this->run.clear();
    this->wait_ms = 0;
  }
  // Keeps the run, so another failure waits longer.
  void end_wait() { this->wait_ms = 0; }
  bool waiting(uint32_t now) const { return this->wait_ms != 0 && now - this->since_ms < this->wait_ms; }
  uint32_t left(uint32_t now) const { return this->waiting(now) ? this->wait_ms - (now - this->since_ms) : 0; }
};

// Alone, the scan's own backoff counts the failure.
inline uint32_t host_scan_result(HostWait &w, bool read, bool failed, bool others, bool refused,
                                 uint32_t retry_after_ms, uint32_t rnd, uint32_t now) {
  if (!read)
    return 0;
  if (!failed) {
    w.clear();
    return 0;
  }
  return others ? w.fail(refused, retry_after_ms, rnd, now) : 0;
}

// Counts even alone: no mode backoff counts side reads.
inline uint32_t host_side_result(HostWait &w, bool read, bool failed, bool refused, uint32_t retry_after_ms,
                                 uint32_t rnd, uint32_t now) {
  if (!read)
    return 0;
  if (!failed) {
    w.clear();
    return 0;
  }
  return w.fail(refused, retry_after_ms, rnd, now);
}

// Baseball TBD is doubleheader game 2: poll it as near.
static const int64_t kPreNearSeconds = 60 * 60;
inline bool pre_poll_near(int64_t until_s, bool time_tbd, bool baseball) {
  if (!time_tbd)
    return until_s <= kPreNearSeconds;
  return baseball && until_s <= 0 && until_s > -30 * 3600;
}

// One more read missed. The ticker and the device page warn from three in a
// row, so the count stops at 255 instead of wrapping to 0 and dropping the
// warning while ESPN is still out of reach.
inline uint8_t add_miss(uint8_t misses) { return misses < 255 ? (uint8_t) (misses + 1) : misses; }

// Whether a soccer cup job (one cup's team read, then the game poll when the
// pick moved) must back off. A poll is the job's last read, so its result
// decides. Without one, a failed or refused cup read backs off.
inline bool cup_read_backs_off(bool cup_ok, bool polled, bool game_ok, bool throttled) {
  return polled ? !game_ok : (!cup_ok || throttled);
}

// Only the waiting host's reads wait; others go on.
inline uint32_t held_next_ms(uint32_t scan_left, uint32_t held_left, bool scan_held) {
  if (held_left == 0)
    return scan_left;
  if (scan_left == 0)
    return scan_held ? held_left : 0;
  return held_left < scan_left ? held_left : scan_left;
}

}  // namespace espn
