// ARMOR-ELECTRICAL - puts what is safe in the external memory (PSRAM) and keeps the small internal memory for what needs it. Nothing here has run on a board; the rules are in
// core/memory_budget.hpp and are tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "psram_mem.hpp"

#include <atomic>
extern "C" {
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/platform.h"
#include "sdkconfig.h"
}

namespace armor::memory {
namespace {
constexpr char kTag[] = "armor-memory";
std::atomic<unsigned> g_stacks{0};
std::atomic<std::uint32_t> g_stack_bytes{0};
std::atomic<unsigned> g_tls_fallbacks{0};   // TLS requests the external RAM could not serve (counted here: the allocator must not log, it can be called from anywhere)

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr std::uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

void* tls_calloc(std::size_t count, std::size_t size) {
  void* memory = heap_caps_calloc(count, size, kExternalCaps);
  if (memory == nullptr) {
    memory = heap_caps_calloc(count, size, kInternalCaps);
    if (memory != nullptr) g_tls_fallbacks.fetch_add(1, std::memory_order_relaxed);
  }
  return memory;
}

void tls_free(void* memory) { heap_caps_free(memory); }

void monitor_task(void*) {
  membudget::LogState state;
  unsigned reported_fallbacks = 0;
  for (;;) {
    const membudget::Snapshot now = snapshot();
    const std::uint64_t now_ms = static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
    const membudget::LogAction action = membudget::next_log(state, now_ms, now);
    if (action != membudget::LogAction::kNone) {
      const std::string line = membudget::describe(now, stacks_in_psram(), stack_bytes_in_psram());
      if (action == membudget::LogAction::kWarn) ESP_LOGW(kTag, "%s - the internal memory is low", line.c_str());
      else ESP_LOGI(kTag, "%s", line.c_str());
      const unsigned fallbacks = g_tls_fallbacks.load(std::memory_order_relaxed);
      if (fallbacks != reported_fallbacks) {
        ESP_LOGW(kTag, "%u TLS requests had to use internal memory (the external RAM was missing or full)", fallbacks - reported_fallbacks);
        reported_fallbacks = fallbacks;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
}  // namespace

void install_tls_allocator() {
  if (mbedtls_platform_set_calloc_free(&tls_calloc, &tls_free) != 0) ESP_LOGE(kTag, "the TLS memory could not be redirected: mbedTLS keeps its default allocator");
}

membudget::Snapshot snapshot() {
  membudget::Snapshot s;
  s.internal_free = static_cast<std::uint32_t>(heap_caps_get_free_size(kInternalCaps));
  s.internal_min = static_cast<std::uint32_t>(heap_caps_get_minimum_free_size(kInternalCaps));
  s.internal_largest = static_cast<std::uint32_t>(heap_caps_get_largest_free_block(kInternalCaps));
  s.psram_total = static_cast<std::uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  s.psram_free = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  return s;
}

void* allocate(std::size_t bytes, const char* what) {
  const membudget::Choice choice = membudget::choose_place(true, bytes, snapshot());
  void* memory = nullptr;
  if (choice.place == membudget::Place::kPsram) memory = heap_caps_malloc(bytes, kExternalCaps);
  if (memory != nullptr) return memory;
  memory = heap_caps_malloc(bytes, kInternalCaps);
  ESP_LOGW(kTag, "%s: %u bytes taken from the internal memory (the external RAM is absent or full)%s", what, static_cast<unsigned>(bytes), memory == nullptr ? ", and there is none left" : "");
  return memory;
}

void release(void* memory) { heap_caps_free(memory); }

bool start_task(TaskFunction_t function, const char* name, std::uint32_t stack_bytes, void* argument, UBaseType_t priority, TaskHandle_t* handle, bool fall_back_to_internal) {
  if (handle != nullptr) *handle = nullptr;
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
  const membudget::Choice choice = membudget::choose_place(true, stack_bytes, snapshot());
  if (choice.place == membudget::Place::kPsram) {
    // The control block stays internal (the kernel requires it); the stack, 16-byte aligned, is the part that moves.
    StackType_t* stack = static_cast<StackType_t*>(heap_caps_aligned_alloc(16, stack_bytes, kExternalCaps));
    StaticTask_t* block = static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), kInternalCaps));
    TaskHandle_t created = (stack != nullptr && block != nullptr) ? xTaskCreateStatic(function, name, stack_bytes, argument, priority, stack, block) : nullptr;
    if (created != nullptr) {
      if (handle != nullptr) *handle = created;
      g_stacks.fetch_add(1, std::memory_order_relaxed);
      g_stack_bytes.fetch_add(stack_bytes, std::memory_order_relaxed);
      ESP_LOGI(kTag, "task \"%s\": its %u-byte stack is in the external RAM", name, static_cast<unsigned>(stack_bytes));
      return true;
    }
    heap_caps_free(stack);
    heap_caps_free(block);
  }
  ESP_LOGW(kTag, "task \"%s\": its %u-byte stack %s (no external RAM free)", name, static_cast<unsigned>(stack_bytes), fall_back_to_internal ? "is in the internal memory" : "was not made");
#endif
  if (!fall_back_to_internal) return false;
  return xTaskCreate(function, name, stack_bytes, argument, priority, handle) == pdPASS;
}

unsigned stacks_in_psram() { return g_stacks.load(std::memory_order_relaxed); }
std::uint32_t stack_bytes_in_psram() { return g_stack_bytes.load(std::memory_order_relaxed); }

void start_monitor() { start_task(monitor_task, "mem-watch", 3072, nullptr, 1); }

}  // namespace armor::memory
