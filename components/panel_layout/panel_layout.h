#pragma once

#include "esphome/components/hub75/hub75_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "layouts.h"

namespace esphome {
namespace panel_layout {

// Must run before the display sizes its DMA buffers.
class PanelLayout : public Component {
 public:
  void set_display(hub75::HUB75Display *display) { this->display_ = display; }
  void set_default_cols(uint8_t cols) { this->default_cols_ = cols; }
  void set_max_cols(uint8_t cols) { this->max_cols_ = cols; }
  void set_max_rows(uint8_t rows) { this->max_rows_ = rows; }

  uint8_t cols() const { return this->cols_; }
  uint8_t rows() const { return this->rows_; }
  uint8_t wiring() const { return this->wiring_; }
  bool bluetooth_ok() const { return panel_layouts::bluetooth_ok(this->cols_, this->rows_); }
  // Reboots a second later so the page can answer.
  bool set_layout(uint8_t cols, uint8_t rows);
  void set_cols(uint8_t cols) { this->set_layout(cols, 1); }
  // Always saved; restarts only when the layout is 2x2.
  bool set_wiring(uint8_t wiring);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR + 10.0f; }

 protected:
  // v1 key kept in step so downgrades keep the layout.
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
