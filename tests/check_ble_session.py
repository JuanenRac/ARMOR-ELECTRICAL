#!/usr/bin/env python3
# ARMOR-ELECTRICAL - guard on the Bluetooth set-up's shared state (main/ble_provision.cpp). Nothing is compiled: it reads the source and fails when
#   1. the session of the phone (g_session) is used by a function that does not take its lock: the radio's host task clears it on every connection while the worker task uses it;
#   2. a request no longer carries the number of the connection it arrived in, or the worker no longer drops a request (and an answer) whose connection is gone;
#   3. a connection or a disconnection no longer changes that number under the lock, or no longer empties the queue of requests that were waiting, which is how a request of one phone was
#      answered, with the session of the next one, to a phone that did not ask.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def problems_of(text: str) -> list[str]:
    problems: list[str] = []
    if not text:
        return ["main/ble_provision.cpp is missing"]
    if not re.search(r"std::mutex\s+g_session_lock\b", text):
        problems.append("there is no std::mutex g_session_lock for the session of the phone")
    # every function that touches the session takes the lock (the chunks end at a closing brace in the first column)
    for chunk in re.split(r"\n\}\s*;?\s*\n", text):
        if re.search(r"\bg_session\b(?!_)", chunk) and "g_session_lock" not in chunk:
            code = [line for line in chunk.strip().splitlines() if line.strip() and not line.strip().startswith("//")]
            problems.append(f"a function uses g_session without g_session_lock: {(code[0] if code else '')[:80]!r}")
    if not re.search(r"struct Pending\s*\{[^}]*unsigned\s+generation", text):
        problems.append("a queued request does not carry the generation (the number of the connection it arrived in)")
    if not re.search(r"new Pending\{\s*g_generation\.load\(\)", text):
        problems.append("a request is queued without the generation it arrived in")
    worker = re.search(r"void worker_task\(void\*\)\s*\{.*?\n\}\n", text, re.S)
    if not worker:
        problems.append("there is no worker_task")
    else:
        body = worker.group(0)
        if len(re.findall(r"pending->generation\s*!=\s*g_generation\.load\(\)", body)) < 2:
            problems.append("worker_task must drop a request whose connection is gone twice: before handling it and before keeping the session it changed")
        if "send_message(" not in body or "pending->generation" not in body.split("send_message(")[-1]:
            problems.append("the answer is not sent with the generation of the request")
    send = re.search(r"void send_message\([^)]*\)\s*\{.*?\n\}\n", text, re.S)
    if not send or len(re.findall(r"generation\s*!=\s*g_generation\.load\(\)", send.group(0))) < 2:
        problems.append("send_message must check the generation before and while it sends")
    if not re.search(r"void drain_queue\(\)\s*\{[^}]*xQueueReceive", text, re.S):
        problems.append("there is no drain_queue that empties the waiting requests")
    gap = re.search(r"int on_gap\(.*?\n\}\n", text, re.S)
    if not gap:
        problems.append("there is no on_gap")
    else:
        body = gap.group(0)
        for event in ("BLE_GAP_EVENT_CONNECT", "BLE_GAP_EVENT_DISCONNECT"):
            part = body.split("case " + event + ":", 1)
            if len(part) < 2:
                problems.append(f"on_gap does not handle {event}")
                continue
            section = re.split(r"\n    case BLE_GAP_EVENT_", part[1], maxsplit=1)[0]
            if "++g_generation" not in section or "g_session_lock" not in section:
                problems.append(f"{event} does not change the generation under the lock of the session")
            if "drain_queue()" not in section:
                problems.append(f"{event} does not empty the queue of waiting requests")
    return problems


def main() -> int:
    path = ROOT / "main" / "ble_provision.cpp"
    problems = problems_of(path.read_text(encoding="utf-8", errors="replace") if path.is_file() else "")
    if problems:
        print("BLE_SESSION=FAIL")
        for problem in problems:
            print("  " + problem)
        return 1
    print("BLE_SESSION=PASS the session of the phone is under its lock, and requests and answers belong to the connection they came in")
    return 0


if __name__ == "__main__":
    sys.exit(main())
