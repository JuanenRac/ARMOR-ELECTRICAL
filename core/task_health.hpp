// ARMOR-ELECTRICAL - how the tasks of the node are doing: the stack each has left, as the panel shows it. No hardware.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A task whose stack is nearly used up is a crash waiting for the right input (a long JSON, a deep call). FreeRTOS keeps, for every task, the least stack it has ever had free (the "high-water
// mark"); the firmware reads them (main/task_health.cpp) and this file turns them into the list the panel shows: the tasks with the least room first, and a flag on any that has less than
// `kLowStackBytes` left. The tasks are handed in as plain data, so the ordering and the flag are tested on a computer.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"

namespace armor::health {

constexpr std::uint32_t kLowStackBytes = 512;

struct TaskInfo {
  std::string name;
  std::uint32_t stack_free = 0;   // the least stack ever left free, in bytes
  int priority = 0;
  char state = '?';               // R running, r ready, B blocked, S suspended, D deleted
};

inline bool is_low(const TaskInfo& task) { return task.stack_free < kLowStackBytes; }

// The tasks with the least room first (the name breaks a tie), as the array the status carries: [{"name":..,"stack_free":..,"priority":..,"state":"B","low":false}, ...].
inline std::string tasks_json(std::vector<TaskInfo> tasks) {
  std::sort(tasks.begin(), tasks.end(), [](const TaskInfo& a, const TaskInfo& b) { return a.stack_free != b.stack_free ? a.stack_free < b.stack_free : a.name < b.name; });
  json::Writer w;
  w.begin_array();
  for (const TaskInfo& task : tasks) {
    w.begin_object().field("name", task.name).field("stack_free", static_cast<long long>(task.stack_free)).field("priority", task.priority).field("state", std::string(1, task.state)).field("low", is_low(task)).end_object();
  }
  w.end_array();
  return w.str();
}

// How many of them are short of stack: what a person looks at first.
inline int low_count(const std::vector<TaskInfo>& tasks) {
  int n = 0;
  for (const TaskInfo& task : tasks) if (is_low(task)) ++n;
  return n;
}

}  // namespace armor::health
