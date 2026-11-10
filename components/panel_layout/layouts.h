// No ESPHome includes: set_request.h and host tests use it.
#pragma once

#include <cstdint>

namespace panel_layouts {

// Chain runs along the top row, then back underneath.
enum Wiring : uint8_t { WIRING_FOLDED = 0, WIRING_UPRIGHT = 1, WIRING_TOP_FLIPPED = 2 };
constexpr int kWiringCount = 3;

// The 2x2 is the only grid the pages lay out.
inline bool layout_ok(int cols, int rows, int max_cols, int max_rows) {
  if (rows == 1)
    return cols >= 1 && cols <= max_cols;
  return rows == 2 && rows <= max_rows && cols == 2 && cols * rows <= max_cols;
}

// 3+ panels' DMA buffers leave Bluetooth too little RAM.
inline bool bluetooth_ok(int cols, int rows) { return cols * rows <= 2; }

}  // namespace panel_layouts
