#pragma once

// The crash report's pure parts, with no ESPHome includes, so the host tests
// (tests/test_parse.cpp) compile them directly: the saved record, which
// starts save one, the names it gives them, and how the crash handler's log
// lines become its text.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace crash_report {

// Room for the crash handler's lines: the reason, the PC, the faulting
// address and up to 16 backtrace addresses for each core, about 1.2 KB.
static constexpr size_t REPORT_SIZE = 1536;
static constexpr size_t VERSION_SIZE = 24;

// Saved in flash as is (crash_report.cpp). A different size reads as no
// record, so a change here drops the report a panel had saved.
struct Record {
  uint32_t count;       // unexpected restarts since the last clear; 0 is none
  uint32_t time;        // the clock on the boot after the crash (Unix time), 0 until it had one
  uint32_t build_time;  // ESPHOME_BUILD_TIME of the firmware that saved it
  uint8_t reason;       // esp_reset_reason_t of that boot
  char version[VERSION_SIZE];
  char report[REPORT_SIZE];  // the crash handler's lines, one per line; empty for a brownout
};
static_assert(sizeof(Record) == 1576, "the saved crash report changed size");

// esp_reset_reason_t from esp_system.h, by value (crash_report.cpp checks them).
static constexpr int RESET_PANIC = 4;
static constexpr int RESET_INT_WDT = 5;
static constexpr int RESET_TASK_WDT = 6;
static constexpr int RESET_WDT = 7;
static constexpr int RESET_BROWNOUT = 9;

// A panic or a watchdog: a start the crash handler can have a report for.
inline bool reset_crashed(int reason) {
  return reason == RESET_PANIC || reason == RESET_INT_WDT || reason == RESET_TASK_WDT || reason == RESET_WDT;
}

// The "reason" in GET /gameday/crash, which scripts/crash_report.py prints.
inline const char *reason_name(int reason) {
  switch (reason) {
    case RESET_PANIC:
      return "crash";
    case RESET_INT_WDT:
      return "interrupt watchdog";
    case RESET_TASK_WDT:
      return "task watchdog";
    case RESET_WDT:
      return "watchdog";
    case RESET_BROWNOUT:
      return "brownout";
    default:
      return "other";
  }
}

// Adds one of the crash handler's log lines to the report and returns its new
// length. report holds size bytes, len of them in use, and stays
// NUL-terminated. message is the whole console line: a color code,
// "[E][esp32.crash:123]", ": ", the text and a color reset; only the text is
// kept, one per line. The addr2line hints repeat the addresses above them
// (the script builds its own command), so they are left out, and so is a
// line that doesn't fit.
inline size_t report_append(char *report, size_t size, size_t len, const char *message, size_t message_len) {
  const char *end = message + message_len;
  const char *start = nullptr;
  for (const char *p = message; p + 1 < end; p++) {
    if (p[0] == ':' && p[1] == ' ') {
      start = p + 2;
      break;
    }
  }
  if (start == nullptr)
    return len;
  if (end - start >= 4 && memcmp(end - 4, "\033[0m", 4) == 0)
    end -= 4;
  const size_t n = end - start;
  if ((n >= 5 && memcmp(start, "Use: ", 5) == 0) || (n >= 21 && memcmp(start, "Other core: addr2line", 21) == 0))
    return len;
  if (len + n + 2 > size)
    return len;
  memcpy(report + len, start, n);
  len += n;
  report[len++] = '\n';
  report[len] = '\0';
  return len;
}

}  // namespace crash_report
}  // namespace esphome
