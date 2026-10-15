// ARMOR-ELECTRICAL - Improv Wi-Fi over the USB serial line: a web page (ESP Web Tools) can give the node its Wi-Fi network without a phone or a cable to the router.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once

namespace armor::improv_serial {

// Starts the task that listens on the console's serial line for Improv packets (core/improv.hpp). Whatever else is typed or logged on that line is ignored. Call it once, after the
// network and the settings store are up.
void start();

}  // namespace armor::improv_serial
