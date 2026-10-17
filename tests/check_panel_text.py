#!/usr/bin/env python3
"""Check the panel's texts against the firmware: every row has the eight languages, and every error code the firmware can send has a text.

Usage:  python tests/check_panel_text.py

The panel shows `e_<code>` for an error the node answers with; a code without a row would reach the person as the bare code. The codes are read from the C++ (`send_error(r, N, "code")`
and the `error = "code"` of the updater), so a new code cannot be added without its eight translations.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANGUAGES = 8


def rows(text):
    found = {}
    for match in re.finditer(r"^  ([A-Za-z0-9_]+): \[(.*)\],?$", text, re.M):
        body = match.group(2)
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        found[match.group(1)] = strings
    return found


def main():
    text = (ROOT / "panel" / "text.js").read_text(encoding="utf-8")
    table = rows(text)
    problems = []
    for key, strings in table.items():
        if len(strings) != LANGUAGES:
            problems.append(f"{key}: {len(strings)} texts instead of {LANGUAGES}")
        elif any(not s.strip() for s in strings):
            problems.append(f"{key}: an empty text")
    codes = set()
    for name in ("web_server.cpp", "github_update.cpp", "api_shared.cpp", "ble_provision.cpp"):
        source = (ROOT / "main" / name).read_text(encoding="utf-8")
        codes |= set(re.findall(r'send_error\(r, \d+, "([a-z_0-9]+)"', source))
        codes |= set(re.findall(r'error = "([a-z_0-9]+)"', source))
    codes.discard("http_")   # http_<status> is told by one row, e_http
    for code in sorted(codes):
        if "e_" + code not in table:
            problems.append(f"the firmware answers \"{code}\" and the panel has no text e_{code}")
    if "e_http" not in table:
        problems.append("e_http is missing (the download's HTTP status)")
    if problems:
        print("PANEL_TEXT=FAIL")
        for problem in problems:
            print("  " + problem)
        return 1
    print(f"PANEL_TEXT=PASS {len(table)} rows in {LANGUAGES} languages, {len(codes)} error codes with a text")
    return 0


if __name__ == "__main__":
    sys.exit(main())
