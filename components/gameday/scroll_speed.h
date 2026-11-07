// The ticker's speed: the Scroll Speed setting, 1 to 10, as pixels per
// second, and the scroll's animation time for a line of a given width
// (firmware/pages/gameday-live.yaml). Pure, with no ESPHome or LVGL includes,
// so the host tests compile it directly.
#pragma once

#include <cmath>
#include <cstdint>

namespace esphome::gameday {

// 1 is 8 px/s, about one and a half letters of the 5 px wide ticker font a
// second; 10 is 32 px/s, about six. Each step is the same 17% faster than the
// one before, so the low end has as much room to choose as the high end. The
// boot screen's lines, for reading from across a room, scroll at 12 px/s.
constexpr float kTickerSlowestPxPerS = 8.0f;
constexpr float kTickerFastestPxPerS = 32.0f;

inline float ticker_px_per_s(float setting) {
  if (!(setting >= 1.0f))  // below the range, or NaN
    setting = 1.0f;
  if (setting > 10.0f)
    setting = 10.0f;
  return kTickerSlowestPxPerS * std::pow(kTickerFastestPxPerS / kTickerSlowestPxPerS, (setting - 1.0f) / 9.0f);
}

// LVGL scrolls a circular label over a fixed time, not at a fixed speed, so
// the time follows the distance: the line's width plus the gap LVGL leaves
// before it comes round again (about three spaces). At least 1 ms, since 0
// would stop the scroll.
inline uint32_t ticker_scroll_ms(int32_t line_px, int32_t gap_px, float setting) {
  const int32_t px = line_px + gap_px;
  if (px <= 0)
    return 1;
  const uint32_t ms = (uint32_t) std::lround((float) px * 1000.0f / ticker_px_per_s(setting));
  return ms > 0 ? ms : 1;
}

}  // namespace esphome::gameday
