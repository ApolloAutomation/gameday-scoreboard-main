// The panel arrangements the firmware drives and how a saved or asked-for
// one is checked. Pure, no ESPHome includes: the device page's parser
// (components/gameday/set_request.h) and the host tests read it too.
#pragma once

#include <cstdint>

namespace panel_layouts {

// How the second row of a 2x2 is mounted. The chain runs from the panel with
// the controller to the one beside it in the top row, then down to the panel
// under that one and back along the bottom row.
//   FOLDED      the bottom row turned upside down under the top row (the
//               shortest cables); the top row stands the way one panel does.
//   UPRIGHT     every panel the right way up, a longer cable from the end of
//               the top row back to the start of the bottom row.
//   TOP_FLIPPED the top row upside down and the bottom row the right way up.
enum Wiring : uint8_t { WIRING_FOLDED = 0, WIRING_UPRIGHT = 1, WIRING_TOP_FLIPPED = 2 };
constexpr int kWiringCount = 3;

// One row of up to max_cols panels, or the 2x2, the one grid the pages lay
// out. max_cols is the longest chain the build drives.
inline bool layout_ok(int cols, int rows, int max_cols, int max_rows) {
  if (rows == 1)
    return cols >= 1 && cols <= max_cols;
  return rows == 2 && rows <= max_rows && cols == 2 && cols * rows <= max_cols;
}

// Bluetooth setup needs the internal RAM a third and fourth panel's DMA
// buffers take, so more than two panels set up through the hotspot only.
inline bool bluetooth_ok(int cols, int rows) { return cols * rows <= 2; }

}  // namespace panel_layouts
