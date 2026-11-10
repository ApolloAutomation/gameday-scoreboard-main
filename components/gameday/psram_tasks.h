// Task stacks in PSRAM; internal RAM is for DMA buffers.
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
  // Thread-safe: the fetch task starts logo downloads.
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
  // Never self-delete: IDF can't free a running stack.
  [[noreturn]] static void park() {
    for (;;)
      vTaskSuspend(nullptr);
  }
  // Call from the main loop only.
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
