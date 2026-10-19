// ARMOR-ELECTRICAL - the node's own watchdog over its health (rules in core/node_guard.hpp, tested on a computer). Nothing here has run on a board.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include "core/node_guard.hpp"

extern "C" {
#include "esp_err.h"
#include "esp_http_server.h"
}

namespace armor::nodeguard {

// Says that a task which runs for ever is needed by the node: call it right before the task is made, so a task that could not be made (no memory) is missed all the same. The name is the one the
// task is made with (FreeRTOS keeps its first 15 characters).
void expect_task(const char* name);

// What this boot learned from the ledger of restarts: reads the memory that survives a software reset the first time it is called (call it early, before anything that depends on it).
BootInfo boot_info();

// Starts the task that looks at the node every few seconds, and the handler of a failed allocation. Called once, first thing, by memory::start_monitor().
void start();

// The link to the broker tells the guard: a broker is set up (call once), the connection came or went, a message was handed to the client (the dead-man switch: no message for ten minutes while the
// broker is connected restarts the node, three times an hour at most; then it stays up, degraded).
void note_broker_in_use();
void note_broker_connected(bool connected);
void note_publish();

// An image is being written to the flash: an allocation that fails then is not turned into a clean restart.
void note_firmware_write(bool writing);

// False while the memory is short (core/node_guard.hpp, Gate): the panel's servers refuse new connections.
bool accept_gate_open();
// The `open_fn` of the HTTP servers: refuses a new connection (ESP_FAIL closes the socket) while the gate is closed, before anything is allocated for it.
esp_err_t http_open_gate(httpd_handle_t server, int socket_fd);

}  // namespace armor::nodeguard
