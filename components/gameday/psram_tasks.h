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
  // id: never reused, for running() and cut().
  static bool start(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg, uint32_t *id = nullptr) {
    // Held until listed: the task's cut_self() waits for it.
    std::lock_guard<std::mutex> guard(lock_());
    TaskHandle_t h = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack_bytes, arg, 1, &h, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS &&
        xTaskCreatePinnedToCoreWithCaps(fn, name, stack_bytes, arg, 1, &h, tskNO_AFFINITY,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS)
      return false;
    static uint32_t last_id = 0;
    if (++last_id == 0)
      last_id = 1;
    tasks_().push_back(Task{h, last_id, false});
    if (id != nullptr)
      *id = last_id;
    return true;
  }
  // Started and not parked yet.
  static bool running(uint32_t id) {
    std::lock_guard<std::mutex> guard(lock_());
    const Task *t = find_(id);
    return t != nullptr && eTaskGetState(t->h) != eSuspended;
  }
  // Asks a task given up on to stop its reads.
  static void cut(uint32_t id) {
    std::lock_guard<std::mutex> guard(lock_());
    Task *t = find_(id);
    if (t != nullptr)
      t->cut = true;
  }
  static bool cut_self() {
    std::lock_guard<std::mutex> guard(lock_());
    const TaskHandle_t me = xTaskGetCurrentTaskHandle();
    for (const Task &t : tasks_())
      if (t.h == me)
        return t.cut;
    return false;
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
      if (eTaskGetState(v[i].h) == eSuspended) {
        vTaskDeleteWithCaps(v[i].h);
        v.erase(v.begin() + i);
      } else {
        i++;
      }
    }
  }

 protected:
  struct Task {
    TaskHandle_t h;
    uint32_t id;
    bool cut;
  };
  // Caller holds the lock.
  static Task *find_(uint32_t id) {
    for (Task &t : tasks_())
      if (t.id == id)
        return &t;
    return nullptr;
  }
  static std::vector<Task> &tasks_() {
    static std::vector<Task> tasks;
    return tasks;
  }
  static std::mutex &lock_() {
    static std::mutex lock;
    return lock;
  }
};

}  // namespace gameday
}  // namespace esphome
