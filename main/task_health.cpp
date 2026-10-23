// ARMOR-NODE-ELECTRICAL - the stack each task of the node has left. Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "task_health.hpp"

#include <memory>
#include <mutex>
#include <new>
#include <set>
extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace armor::taskhealth {
namespace {
constexpr char kTag[] = "armor-tasks";
}

std::vector<health::TaskInfo> tasks() {
  std::vector<health::TaskInfo> out;
#if (configUSE_TRACE_FACILITY == 1)
  const UBaseType_t count = uxTaskGetNumberOfTasks();
  if (count == 0 || count > 64) return out;
  std::unique_ptr<TaskStatus_t[]> status(new (std::nothrow) TaskStatus_t[count]);
  if (!status) return out;
  const UBaseType_t got = uxTaskGetSystemState(status.get(), count, nullptr);
  for (UBaseType_t i = 0; i < got; ++i) {
    health::TaskInfo info;
    info.name = status[i].pcTaskName != nullptr ? status[i].pcTaskName : "?";
    info.stack_free = static_cast<std::uint32_t>(status[i].usStackHighWaterMark);   // in bytes on ESP-IDF
    info.priority = static_cast<int>(status[i].uxCurrentPriority);
    switch (status[i].eCurrentState) {
      case eRunning: info.state = 'R'; break;
      case eReady: info.state = 'r'; break;
      case eBlocked: info.state = 'B'; break;
      case eSuspended: info.state = 'S'; break;
      case eDeleted: info.state = 'D'; break;
      default: info.state = '?'; break;
    }
    out.push_back(std::move(info));
  }
#endif
  return out;
}

std::string json() { return health::tasks_json(tasks()); }

void warn_if_low() {
  static std::set<std::string> said;
  static std::mutex said_lock;   // more than one task asks (the health report and the web panel)
  for (const health::TaskInfo& task : tasks()) {
    if (!health::is_low(task)) continue;
    { std::lock_guard<std::mutex> guard(said_lock); if (!said.insert(task.name).second) continue; }
    ESP_LOGW(kTag, "the task \"%s\" has only %u bytes of stack left at its worst: it may overflow", task.name.c_str(), static_cast<unsigned>(task.stack_free));
  }
}

}  // namespace armor::taskhealth
