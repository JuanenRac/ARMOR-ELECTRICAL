#!/usr/bin/env python3
# ARMOR-NODE-ELECTRICAL - guard on what happens when the panel saves the settings. Nothing is compiled: it reads main/ and panel/ and fails when
#   1. api::put_config no longer applies a new time zone at once (clocksync::set_zone) or no longer leaves the zone out of the "restart needed" comparison, which is how a zone
#      saved in the panel stayed on the old one until somebody restarted the node;
#   2. the clock still applies the zone only at start;
#   3. the panel can no longer tell the person that a restart is needed (the restart_required answer, the note in eight languages and the Restart button);
#   4. put_config keeps a whole config::Settings on the stack (the web, Bluetooth and serial set-up paths all run it in a task with a small stack);
#   5. the settings are no longer read tolerantly, a serial set-up no longer sets the Wi-Fi as the way in, or the update check loses its answer for a release without an image for the board.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(relative: str) -> str:
    path = ROOT / relative
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def problems_of() -> list[str]:
    problems: list[str] = []
    api = read("main/api_shared.cpp")
    core = "\n".join(read(p) for p in ("main/core/node_config.hpp", "main/core/node_settings.hpp", "main/core/config_common.hpp")) + api
    clock = read("main/clock_sync.cpp")
    put = re.search(r"PutResult put_config\(.*?\n\}\n", api, re.S)
    if not put:
        return ["main/api_shared.cpp has no put_config"]
    body = put.group(0)
    if "clocksync::set_zone(" not in body:
        problems.append("put_config does not call clocksync::set_zone: a time zone saved in the panel would only apply after a restart")
    if not re.search(r"needs_restart[^{;]*\{[^}]*time\.zone\s*=", core, re.S):
        problems.append("needs_restart compares the time zone: saving only a zone would ask for a restart although it is applied at once")
    if "set_zone" not in clock or not re.search(r"void start\([^)]*\)\s*\{[^}]*set_zone\(", clock, re.S):
        problems.append("clock_sync.cpp: start() does not apply the stored zone through set_zone")
    if re.search(r"\bconfig::Settings\s+\w+\s*(=|;)", body):
        problems.append("put_config keeps a config::Settings on the stack: use the heap (std::unique_ptr / new (std::nothrow))")
    web = read("main/web_server.cpp")
    if "restart_required" not in web or "send_ok(r, restart_required)" not in web:
        problems.append("the web server no longer answers a save with restart_required")
    text = read("panel/text.js")
    row = re.search(r"^  savedRestart: \[(.*)\],?$", text, re.M)
    if not row or len(re.findall(r'"((?:[^"\\]|\\.)*)"', row.group(1))) != 8:
        problems.append("panel/text.js: savedRestart has no text in the eight languages")
    app = read("panel/app.js")
    if "restart_required" not in app or "savedRestart" not in app or "reboot()" not in app:
        problems.append("panel/app.js: the panel no longer shows the restart note and the Restart button when a save needs a restart")
    # what an update and a first set-up leave behind (each of these was a node that came out of an update, or out of a set-up by cable, without its network or its release)
    store = read("main/node_store.cpp")
    if "load_stored(" not in store:
        problems.append("main/node_store.cpp no longer reads the stored settings with load_stored: one value that a newer firmware refuses would throw away all of them (the network and the broker too)")
    improv = read("main/improv_serial.cpp")
    if improv and '\\"uplink\\":\\"wifi\\"' not in improv:
        problems.append("main/improv_serial.cpp does not set uplink wifi: a board with a cable would take the Wi-Fi of a serial set-up and never use it")
    if improv and "sta_connected" not in improv:
        problems.append("main/improv_serial.cpp says \"provisioned\" without asking whether the station joined its network")
    update = read("main/github_update.cpp")
    if update and "no_asset_for_board" not in update:
        problems.append("main/github_update.cpp does not answer no_asset_for_board when a newer release has no image for this board (the panel would say \"up to date\")")
    limit = re.search(r"kMaxFirmware\s*=\s*(0x[0-9a-fA-F]+|\d+)", update)
    slots = [int(size, 0) for size in re.findall(r"^ota_\d,\s*app,\s*ota_\d,\s*\w+,\s*(0x[0-9a-fA-F]+|\d+)", read("partitions.csv"), re.M)]
    if update and limit and slots and int(limit.group(1), 0) > min(slots):
        problems.append("main/github_update.cpp: kMaxFirmware is larger than an application slot of partitions.csv, so an image that fits nowhere would start to download")
    return problems


def main() -> int:
    problems = problems_of()
    for line in problems:
        print("SAVE_PATHS=FAIL " + line)
    if problems:
        return 1
    print("SAVE_PATHS=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
