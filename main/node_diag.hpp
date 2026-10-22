// ARMOR-ELECTRICAL - what the node can tell an administrator about itself when it misbehaves: the core dump in flash and the diagnostic package (core/diag_package.hpp has the rules and is tested on a computer).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include "esp_http_server.h"
}
#include "core/diag_package.hpp"

namespace armor::node_diag {

// Looks, once, whether the partition made for core dumps holds a valid one (a crash leaves it there until it is erased). Call it early; the answer is kept.
void init();

struct CoreInfo {
  bool present = false;
  std::uint32_t size = 0;
};
CoreInfo core_info();

// The summary of the core dump (the task that crashed, where, the backtrace), or one with has_summary false when there is none or the summary could not be read.
diag::CoreSummary core_summary();

// {"present":false} or {"present":true,"size":N,"summary":{...}} for the panel's administrators.
std::string core_info_json();

// Sends the raw image of the core dump as a download. The CALLER has already checked that the request is an administrator's (a logged-in session of the panel with the admin role): nothing
// here asks again, and no route may reach this without that check. Answers 404 when there is no dump. The image is read from flash in small pieces, never held whole in memory.
esp_err_t send_core_dump(httpd_req_t* request);

// Erases the core dump (after it was downloaded). False when there was nothing or flash failed.
bool erase_core_dump();

// The firmware versions of the sensors the node reads (a radar module, for instance). Optional: a node that has none reports an empty list.
void set_sensor_versions(std::function<std::vector<diag::Sensor>()> provider);

// The package (or the log alone) of a diagnostics order, as the JSON text of the `package` member. Reads the real memory, tasks, log and settings; washes the secrets (core/diag_package.hpp).
std::string build_package(diag::Detail detail);

}  // namespace armor::node_diag
