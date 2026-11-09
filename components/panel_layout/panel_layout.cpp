#include "panel_layout.h"

#include <esp_heap_caps.h>

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace panel_layout {

static const char *const TAG = "panel_layout";

// HUB75Display keeps its Hub75Config protected and only reads it when the
// driver is created in setup(). The class is final, so no derived shim; this
// uses the explicit-instantiation rule instead: naming a protected member as
// a template argument of an explicit instantiation is exempt from access
// checks (C++ [temp.explicit]). The instantiation defines get(ConfigTag) to
// return the pointer-to-member. If a future ESPHome renames config_, makes
// the driver earlier, or otherwise moves things, this stops compiling and the
// runtime panel picker needs another route.
struct ConfigTag {
  using type = Hub75Config hub75::HUB75Display::*;
  friend type get(ConfigTag);
};
template<typename Tag, typename Tag::type M> struct Rob {
  friend typename Tag::type get(Tag) { return M; }
};
template struct Rob<ConfigTag, &hub75::HUB75Display::config_>;

// Driver names locate the controller's panel, seen from the front.
static Hub75PanelLayout grid_layout(uint8_t rows, uint8_t wiring) {
  if (rows < 2)
    return Hub75PanelLayout::HORIZONTAL;
  switch (wiring) {
    case panel_layouts::WIRING_UPRIGHT:
      return Hub75PanelLayout::TOP_RIGHT_DOWN_ZIGZAG;
    case panel_layouts::WIRING_TOP_FLIPPED:
      return Hub75PanelLayout::TOP_LEFT_DOWN;
    default:
      return Hub75PanelLayout::TOP_RIGHT_DOWN;
  }
}

static const char *wiring_name(uint8_t wiring) {
  switch (wiring) {
    case panel_layouts::WIRING_UPRIGHT:
      return "all upright";
    case panel_layouts::WIRING_TOP_FLIPPED:
      return "top row upside down";
    default:
      return "bottom row folded under";
  }
}

void PanelLayout::setup() {
  this->pref_ = global_preferences->make_preference<Saved>(fnv1_hash("panel_layout_v2"));
  this->pref_v1_ = global_preferences->make_preference<uint8_t>(fnv1_hash("panel_layout_cols_v1"));
  Saved s{};
  if (!this->pref_.load(&s) || !panel_layouts::layout_ok(s.cols, s.rows, this->max_cols_, this->max_rows_) ||
      s.wiring >= panel_layouts::kWiringCount) {
    uint8_t old = 0;
    if (this->pref_v1_.load(&old) && panel_layouts::layout_ok(old, 1, this->max_cols_, this->max_rows_)) {
      // v1 key from older firmware, one or two wide.
      s = Saved{old, 1, panel_layouts::WIRING_FOLDED, 0};
      ESP_LOGI(TAG, "Keeping the saved panel count, %u wide", old);
    } else {
      // Saved so an update with another default can't change it.
      s = Saved{this->default_cols_, 1, panel_layouts::WIRING_FOLDED, 0};
      ESP_LOGI(TAG, "No saved panel layout, saving the default %u wide", this->default_cols_);
    }
    this->cols_ = s.cols;
    this->rows_ = s.rows;
    this->wiring_ = s.wiring;
    this->save_();
  } else {
    this->cols_ = s.cols;
    this->rows_ = s.rows;
    this->wiring_ = s.wiring;
  }
  if (this->display_ != nullptr) {
    Hub75Config &cfg = this->display_->*get(ConfigTag());
    cfg.layout_cols = this->cols_;
    cfg.layout_rows = this->rows_;
    cfg.layout = grid_layout(this->rows_, this->wiring_);
  }
  // Log RAM left after DMA buffers, for support.
  this->set_timeout("heap", 20000, [this]() {
    ESP_LOGI(TAG, "%ux%u panels, Bluetooth %s. Internal RAM free: %u bytes, largest block %u", this->cols_,
             this->rows_, this->bluetooth_ok() ? "allowed" : "off",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  });
}

void PanelLayout::save_() {
  Saved s{this->cols_, this->rows_, this->wiring_, 0};
  this->pref_.save(&s);
  // Keep the v1 key for downgrades.
  if (this->rows_ == 1 && this->cols_ <= 2)
    this->pref_v1_.save(&this->cols_);
  global_preferences->sync();
}

void PanelLayout::restart_soon_() {
  App.scheduler.set_timeout(this, "reboot", 1000, []() { App.safe_reboot(); });
}

bool PanelLayout::set_layout(uint8_t cols, uint8_t rows) {
  if (!panel_layouts::layout_ok(cols, rows, this->max_cols_, this->max_rows_)) {
    ESP_LOGW(TAG, "Ignoring panel layout %ux%u (allowed: 1 to %u wide%s)", cols, rows, this->max_cols_,
             panel_layouts::layout_ok(2, 2, this->max_cols_, this->max_rows_) ? ", or 2x2" : "");
    return false;
  }
  if (cols == this->cols_ && rows == this->rows_)
    return true;
  ESP_LOGI(TAG, "Panel layout %ux%u -> %ux%u, restarting", this->cols_, this->rows_, cols, rows);
  this->cols_ = cols;
  this->rows_ = rows;
  this->save_();
  this->restart_soon_();
  return true;
}

bool PanelLayout::set_wiring(uint8_t wiring) {
  if (wiring >= panel_layouts::kWiringCount) {
    ESP_LOGW(TAG, "Ignoring 2x2 wiring %u", wiring);
    return false;
  }
  if (wiring == this->wiring_)
    return true;
  ESP_LOGI(TAG, "2x2 wiring: %s", wiring_name(wiring));
  this->wiring_ = wiring;
  this->save_();
  if (this->rows_ > 1)
    this->restart_soon_();
  return true;
}

void PanelLayout::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Panel layout:\n"
                "  Panels: %u wide, %u high (max %u in the chain)\n"
                "  2x2 wiring: %s\n"
                "  Bluetooth setup: %s",
                this->cols_, this->rows_, this->max_cols_, wiring_name(this->wiring_),
                this->bluetooth_ok() ? "allowed" : "off, more than two panels");
}

}  // namespace panel_layout
}  // namespace esphome
