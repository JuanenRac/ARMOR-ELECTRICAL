// ARMOR-NODE-ELECTRICAL - the stack each task of the node has left (read from FreeRTOS), for the panel's status.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <vector>

#include "core/task_health.hpp"

namespace armor::taskhealth {

// Every task now alive with the least stack it has ever had free. Empty if FreeRTOS cannot say.
std::vector<health::TaskInfo> tasks();

// The same as the JSON array of core/task_health.hpp, ready to put in the status.
std::string json();

// Says in the log, once per task, that it is short of stack (so a near overflow is noticed before the crash). Call it now and then (the panel's status does).
void warn_if_low();

}  // namespace armor::taskhealth
