#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "poll_timing.h"

namespace espn {

struct LogoSlot {
  uint32_t used;  // millis() of the last lookup
  bool busy;      // queued or downloading
  bool pinned;    // on screen right now
};

// Index of the least recently used slot that can be dropped, or -1 when every
// slot is busy or on screen. `now` is millis(): ages, not raw stamps, keep the
// order right across the millis() wrap.
template<class V> inline int pick_evict_in(const V &slots, uint32_t now) {
  int best = -1;
  uint32_t best_age = 0;
  for (size_t i = 0; i < slots.size(); i++) {
    if (slots[i].busy || slots[i].pinned)
      continue;
    uint32_t age = now - slots[i].used;
    if (best < 0 || age > best_age) {
      best = (int) i;
      best_age = age;
    }
  }
  return best;
}

inline int pick_evict(const std::vector<LogoSlot> &slots, uint32_t now) { return pick_evict_in(slots, now); }

// Shown or held cards first, then prefetches, oldest first
enum LogoPrio : uint8_t { LOGO_NOW = 0, LOGO_NEXT = 1 };

inline bool logo_job_before(uint8_t prio, uint32_t seq, uint8_t other_prio, uint32_t other_seq) {
  if (prio != other_prio)
    return prio < other_prio;
  return (int32_t) (seq - other_seq) < 0;
}

// A logo that fails waits longer after each failure in a row, counted per
// logo so one missing logo never holds back the others or the score reads:
// the same 30 s doubling to 10 min the ESPN reads use, and the longer 5 min
// to an hour after a 403 or 429. A logo that loads starts over.
inline void logo_failed(uint8_t &fails, uint32_t &wait_ms, bool throttled, uint32_t retry_after_ms, uint32_t rnd) {
  if (fails < 255)
    fails++;
  wait_ms = backoff_ms(fails, throttled, retry_after_ms, rnd);
}

inline void logo_loaded(uint8_t &fails, uint32_t &wait_ms) {
  fails = 0;
  wait_ms = 0;
}

inline bool logo_retry_due(uint32_t failed_ms, uint32_t wait_ms, uint32_t now) { return now - failed_ms >= wait_ms; }

// Longest wait for new logos before drawing.
static const uint32_t LOGO_HOLD_MS = 3500;

// True once a held board can go up: neither logo is still on its way, or it
// has waited LOGO_HOLD_MS. A failed or refused logo is not on its way, so it
// never holds a board.
inline bool logo_hold_over(bool team_waiting, bool opp_waiting, uint32_t since_ms, uint32_t now) {
  return (!team_waiting && !opp_waiting) || now - since_ms >= LOGO_HOLD_MS;
}

// DRAW_LATE: drawn after waiting, so its turn restarts
enum class HoldStep : uint8_t { DRAW, WAIT, DRAW_LATE };

// fresh: new logos and no splash; old card stays meanwhile
struct LogoHold {
  bool on{false};
  uint32_t since{0};
  HoldStep step(bool fresh, bool team_waiting, bool opp_waiting, uint32_t now) {
    const bool was = this->on;
    if (!fresh) {
      this->on = false;
      return was ? HoldStep::DRAW_LATE : HoldStep::DRAW;
    }
    if (!was)
      this->since = now;
    this->on = !logo_hold_over(team_waiting, opp_waiting, this->since, now);
    if (this->on)
      return HoldStep::WAIT;
    return was ? HoldStep::DRAW_LATE : HoldStep::DRAW;
  }
  // Held, and its logos arrived or time ran out
  bool over(bool team_waiting, bool opp_waiting, uint32_t now) const {
    return this->on && logo_hold_over(team_waiting, opp_waiting, this->since, now);
  }
};

// The plain logo for a dark one (espn::dark_logo), or "" for any other URL.
// A few teams have no dark variant: Fenerbahce's answers 404 (2026-10-14).
inline std::string plain_logo_url(const std::string &url) {
  size_t pos = url.find("/500-dark/");
  if (pos == std::string::npos)
    return "";
  std::string out = url;
  out.replace(pos, 10, "/500/");
  return out;
}

// Per logo URL: dark works, plain only, or none
enum class LogoVariant : uint8_t { DARK = 0, PLAIN, NONE };

struct LogoMemo {
  uint32_t key;    // logo_key() of the URL asked for
  uint32_t since;  // millis() when learned
  LogoVariant kind;
};

static const size_t kLogoMemoCap = 64;
static const uint32_t kLogoNoneMs = 6u * 3600u * 1000u;
static const uint32_t kLogoPlainMs = 24u * 3600u * 1000u;

// FNV-1a, so a slot holds no URL
inline uint32_t logo_key(const char *s, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint8_t) s[i];
    h *= 16777619u;
  }
  return h;
}

inline uint32_t logo_memo_ms(LogoVariant k) { return k == LogoVariant::NONE ? kLogoNoneMs : kLogoPlainMs; }

// DARK when unknown or learned too long ago
template<class V> inline LogoVariant memo_get(const V &memo, uint32_t key, uint32_t now) {
  for (const auto &m : memo)
    if (m.key == key)
      return now - m.since < logo_memo_ms(m.kind) ? m.kind : LogoVariant::DARK;
  return LogoVariant::DARK;
}

// Wait left for a logo known missing, else 0
template<class V> inline uint32_t memo_none_left(const V &memo, uint32_t key, uint32_t now) {
  for (const auto &m : memo)
    if (m.key == key && m.kind == LogoVariant::NONE && now - m.since < kLogoNoneMs)
      return kLogoNoneMs - (now - m.since);
  return 0;
}

// DARK forgets; a full memo replaces its oldest
template<class V>
inline void memo_put(V &memo, uint32_t key, LogoVariant kind, uint32_t now, size_t cap = kLogoMemoCap) {
  for (size_t i = 0; i < memo.size(); i++) {
    if (memo[i].key != key)
      continue;
    if (kind == LogoVariant::DARK) {
      memo.erase(memo.begin() + i);
    } else {
      memo[i].kind = kind;
      memo[i].since = now;
    }
    return;
  }
  if (kind == LogoVariant::DARK || cap == 0)
    return;
  if (memo.size() < cap) {
    memo.push_back(LogoMemo{key, now, kind});
    return;
  }
  size_t old = 0;
  for (size_t i = 1; i < memo.size(); i++)
    if (now - memo[i].since > now - memo[old].since)
      old = i;
  memo[old] = LogoMemo{key, now, kind};
}

// Plain URL first when the dark one is missing
inline std::string logo_first_url(const std::string &url, LogoVariant kind) {
  if (kind != LogoVariant::PLAIN)
    return url;
  std::string plain = plain_logo_url(url);
  return plain.empty() ? url : plain;
}

// Only 200 and 404 teach; timeouts keep the memo
enum class LogoNote : uint8_t { KEEP, DARK, PLAIN, NONE };

inline LogoNote logo_learned(bool has_plain, bool plain_first, int first, int plain) {
  if (plain_first)
    return first == 200 ? LogoNote::PLAIN : first == 404 ? LogoNote::NONE : LogoNote::KEEP;
  if (first == 200)
    return LogoNote::DARK;
  if (first != 404)
    return LogoNote::KEEP;
  if (!has_plain)
    return LogoNote::NONE;
  return plain == 200 ? LogoNote::PLAIN : plain == 404 ? LogoNote::NONE : LogoNote::KEEP;
}

template<class V> inline void memo_note(V &memo, uint32_t key, LogoNote note, uint32_t now) {
  if (note == LogoNote::KEEP)
    return;
  const LogoVariant kind = note == LogoNote::PLAIN  ? LogoVariant::PLAIN
                           : note == LogoNote::NONE ? LogoVariant::NONE
                                                    : LogoVariant::DARK;
  memo_put(memo, key, kind, now);
}

}  // namespace espn
