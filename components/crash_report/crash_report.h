#pragma once

// Keeps the last crash's backtrace where it can still be read days later.
//
// ESPHome's own crash handler (components/esp32/crash_handler.cpp) already
// saves the faulting PC and backtrace when the panel crashes, and prints
// them on the next boot and to the first log client. But it keeps them in
// RAM that an unplug wipes, the first log client clears them, and the boot
// lines never reach a log read over Wi-Fi. A crash on a panel nobody was
// watching left nothing to decode.
//
// On a boot after a crash, a watchdog reset or a brownout, this component
// has the crash handler print its report again, keeps the lines it logs, and
// saves them in flash with the reset reason, the firmware version and its
// build time, plus a count of such restarts since the report was last
// cleared. GET /gameday/crash returns it as JSON and POST /gameday/crash?clear=1
// empties it (gameday.cpp). scripts/crash_report.py fetches it and decodes
// the addresses against the release's ELF.
//
// Only a crash or watchdog boot reads the crash handler: its report also
// survives the soft restarts that follow (OTA, the Reboot button), and those
// must not save it a second time. A restart the firmware asked for that comes
// back as the RTC watchdog is one of them (save_for_start in capture.h). A
// brownout saves the reason alone, since any report still in RAM then is
// from an earlier crash.

#include <atomic>
#include <mutex>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "capture.h"

namespace esphome {
namespace crash_report {

class CrashReport : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // After the preferences are ready and before the API, so a log client
  // that connects and clears the crash handler's report can't beat us to it.
  float get_setup_priority() const override { return setup_priority::BUS; }

  void set_variant(const char *variant) { this->variant_ = variant; }

  // The saved record as JSON. Safe from the HTTP task.
  std::string json();
  // Empties the saved record. Safe from the HTTP task; the flash write runs
  // on the main loop.
  void clear();

 protected:
  void on_log_(const char *tag, const char *message, size_t len);
  void save_();  // main loop only

  Record *record_{nullptr};  // PSRAM, only while a record is saved
  std::mutex mutex_;
  ESPPreferenceObject pref_;
  const char *variant_{""};
  size_t report_len_{0};
  bool capturing_{false};
  bool need_time_{false};
  std::atomic<bool> clear_pending_{false};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern CrashReport *global_crash_report;

}  // namespace crash_report
}  // namespace esphome
