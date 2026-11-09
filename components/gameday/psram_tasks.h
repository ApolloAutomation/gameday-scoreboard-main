// The component's short-lived network tasks (the ESPN fetch, a logo download,
// the Wi-Fi check), with their stacks in PSRAM. Each one's 6 to 16 KB stack
// used to come out of internal RAM, which the panels' DMA buffers need: with
// four panels a fetch and a logo download running together left about 2 KB
// and every HTTPS connection failed. A task created here ends with park(),
// and the main loop frees it with reap(). It never deletes itself: freeing a
// stack it is still running on would need a helper task, and IDF aborts when
// it can't make one, which is just when internal RAM is short.
#pragma once

#include <mutex>
#include <vector>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

namespace esphome {
namespace gameday {

class PsramTasks {
 public:
  // From any task (the fetch task starts logo downloads). The stack goes in
  // PSRAM, or in internal RAM when PSRAM has no room, at priority 1 like the
  // tasks before.
  static bool start(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg) {
    std::lock_guard<std::mutex> guard(lock_());
    TaskHandle_t h = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack_bytes, arg, 1, &h, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS &&
        xTaskCreatePinnedToCoreWithCaps(fn, name, stack_bytes, arg, 1, &h, tskNO_AFFINITY,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS)
      return false;
    tasks_().push_back(h);
    return true;
  }
  // The task's last call: it stops here until reap() frees it.
  [[noreturn]] static void park() {
    for (;;)
      vTaskSuspend(nullptr);
  }
  // Main loop: frees every task that has parked.
  static void reap() {
    std::lock_guard<std::mutex> guard(lock_());
    auto &v = tasks_();
    for (size_t i = 0; i < v.size();) {
      if (eTaskGetState(v[i]) == eSuspended) {
        vTaskDeleteWithCaps(v[i]);
        v.erase(v.begin() + i);
      } else {
        i++;
      }
    }
  }

 protected:
  static std::vector<TaskHandle_t> &tasks_() {
    static std::vector<TaskHandle_t> tasks;
    return tasks;
  }
  static std::mutex &lock_() {
    static std::mutex lock;
    return lock;
  }
};

}  // namespace gameday
}  // namespace esphome
