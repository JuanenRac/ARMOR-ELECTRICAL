// ARMOR-NODE-ELECTRICAL - puts what is safe in the external memory (PSRAM) and keeps the small internal memory for what needs it. The rules are in core/memory_budget.hpp (tested on a computer);
// nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/memory_budget.hpp"

namespace armor::memory {

// Called first thing in app_main: mbedTLS takes its memory from the external RAM and, where there is none (a module without PSRAM, or one whose PSRAM did not start), from the internal one.
// The sdkconfig sets CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC for it: the stock "external" mode answers NULL to every request on a board with no PSRAM, and no TLS would work there at all.
void install_tls_allocator();

// The heap as it is now: internal free, the least it has been, the biggest block, and the external RAM.
membudget::Snapshot snapshot();

// A buffer that lives long and is only ever touched by the CPU (never by DMA, an interrupt or code that runs with the flash busy): external RAM when there is room, otherwise internal
// memory (the log says so). Free it with release(). nullptr only when neither has room.
void* allocate(std::size_t bytes, const char* what);
void release(void* memory);

// xTaskCreate for a task that may live in the external RAM: its stack goes there, which spares the internal memory the whole stack. ONLY for a task that
//   - runs for ever (it never returns and never calls vTaskDelete: its stack and control block would not be freed),
//   - never touches the flash, directly or through a call: no NVS (not even a read), no esp_partition_*, no esp_ota_*, no store::save_*, no esp_restart of its own, and
//   - owns no DMA buffer on its stack and no interrupt handler that reads it.
// With the flash busy the external RAM cannot be read; a task that is not running then is no problem, one that touched the flash from that stack would crash. When the external RAM is
// absent or too full the task is made the usual way, on an internal stack, and the log says so. Returns false only when the task could not be made at all.
// `handle` receives the task. `fall_back_to_internal` false: the task is NOT made at all when its stack cannot go to the external RAM (for a task that is only worth having if it spares the
// internal memory); the answer is false then.
bool start_task(TaskFunction_t function, const char* name, std::uint32_t stack_bytes, void* argument, UBaseType_t priority, TaskHandle_t* handle = nullptr, bool fall_back_to_internal = true);

// How many task stacks and how many bytes of them are in the external RAM.
unsigned stacks_in_psram();
std::uint32_t stack_bytes_in_psram();

// Starts the task that says the state of the memory in the log every minute (and at once when the internal memory turns low or back to normal), and the node's watchdog (main/node_guard.hpp). Call it once, first thing.
void start_monitor();

}  // namespace armor::memory
