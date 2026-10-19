#!/usr/bin/env python3
# ARMOR-ELECTRICAL - guard on what happens when the panel saves the settings. Nothing is compiled: it reads main/ and panel/ and fails when
#   1. api::put_config no longer applies a new time zone at once (clocksync::set_zone) or no longer leaves the zone out of the "restart needed" comparison, which is how a zone
#      saved in the panel stayed on the old one until somebody restarted the node;
#   2. the clock still applies the zone only at start;
#   3. the panel can no longer tell the person that a restart is needed (the restart_required answer, the note in eight languages and the Restart button);
#   4. put_config keeps a whole config::Settings on the stack (the web, Bluetooth and serial set-up paths all run it in a task with a small stack).
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
