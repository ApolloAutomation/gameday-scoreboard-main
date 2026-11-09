#pragma once

#include "esphome/components/hub75/hub75_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "layouts.h"

namespace esphome {
namespace panel_layout {

// Applies a saved panel arrangement (how many panels wide, one row or the
// 2x2, and how a 2x2 is wired) to the HUB75 display before its driver starts,
// then restarts the device when it changes. Runs just ahead of the display in
// setup order; the driver sizes its DMA buffers and LVGL its draw buffer from
// the display after that.
class PanelLayout : public Component {
 public:
  void set_display(hub75::HUB75Display *display) { this->display_ = display; }
  void set_default_cols(uint8_t cols) { this->default_cols_ = cols; }
  void set_max_cols(uint8_t cols) { this->max_cols_ = cols; }
  void set_max_rows(uint8_t rows) { this->max_rows_ = rows; }

  uint8_t cols() const { return this->cols_; }
  uint8_t rows() const { return this->rows_; }
  uint8_t wiring() const { return this->wiring_; }
  // False with more than two panels: their DMA buffers leave too little
  // internal RAM for the Bluetooth stack, so it is never started.
  bool bluetooth_ok() const { return panel_layouts::bluetooth_ok(this->cols_, this->rows_); }
  // Saves a new arrangement and reboots a second later so the page can
  // answer. False, and nothing changes, for one the build can't drive.
  bool set_layout(uint8_t cols, uint8_t rows);
  void set_cols(uint8_t cols) { this->set_layout(cols, 1); }
  // A 2x2's wiring. Saved whatever the arrangement; restarts only a 2x2.
  bool set_wiring(uint8_t wiring);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR + 10.0f; }

 protected:
  // What panel_layout_v2 holds. The older one-byte panel_layout_cols_v1 (one
  // or two panels wide) is read when this one isn't there yet, and kept in
  // step for a choice it can hold, so going back to older firmware keeps it.
  struct Saved {
    uint8_t cols;
    uint8_t rows;
    uint8_t wiring;
    uint8_t reserved;
  };
  void save_();
  void restart_soon_();

  hub75::HUB75Display *display_{nullptr};
  uint8_t default_cols_{1};
  uint8_t max_cols_{2};
  uint8_t max_rows_{1};
  uint8_t cols_{1};
  uint8_t rows_{1};
  uint8_t wiring_{panel_layouts::WIRING_FOLDED};
  ESPPreferenceObject pref_v1_;
  ESPPreferenceObject pref_;
};

}  // namespace panel_layout
}  // namespace esphome
