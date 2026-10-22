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


def check_help_and_frames(table, problems):
    """The help names what the node does today (in every language), and the Summary puts its values in the green frames."""
    needed = {"help_users_text": ["429", "30"], "help_update_text": ["GitHub"], "help_network_text": ["SHA-256", "PIN"], "help_overview_text": ["NTP"]}
    if "help_server_text" in table:
        needed["help_server_text"] = ["SHA-256"]
    if "help_ports_text" in table:
        needed["help_ports_text"] = ["RJ45", "115200", "pwrsys"]
    for key, words in needed.items():
        for language, text in enumerate(table.get(key, [])):
            for word in words:
                if word not in text:
                    problems.append(f"{key}: the text {language} does not mention {word}")
    app = (ROOT / "panel" / "app.js").read_text(encoding="utf-8")
    for name in ("function overviewPage(", "function flashCard("):
        start = app.find(name)
        end = app.find("\n}", start)
        body = app[start:end]
        if start < 0 or "kvf(" not in body or re.search(r"(?<![\w])kv\(", body):
            problems.append(f"{name[9:-1]} must show its values with kvf( (the green frames)")
    if 'const kvf = rows => kv(rows, "kv framed");' not in app:
        problems.append("app.js has no kvf")
    css = (ROOT / "panel" / "style.css").read_text(encoding="utf-8")
    if ".kv.framed dd" not in css:
        problems.append("style.css has no .kv.framed rule")


def check_panel_features(table, problems):
    """The Broker page can load the certificate of the broker from a file (and refuses a key), and the System page has the node's limit on wrong passwords per minute, with their texts."""
    app = (ROOT / "panel" / "app.js").read_text(encoding="utf-8")
    if "function loadCaFile(" not in app or 'tip: "brokerCaUpload"' not in app or "PRIVATE KEY" not in app:
        problems.append("the Broker page cannot load the certificate from a file (loadCaFile, the button, and the refusal of a private key)")
    for key in ("brokerCaUpload", "tip_brokerCaUpload", "brokerCaLoaded", "brokerCaBad", "brokerCaKey", "brokerCaBig", "loginLimit", "tip_loginLimit", "loginLimitNote"):
        if key not in table:
            problems.append(f"the panel has no text {key}")
    if '"system.login_limit_per_minute"' not in app:
        problems.append("the System page does not offer system.login_limit_per_minute")
    if "relayInterlock" in app:
        for key in ("relayInterlock", "relayInterlockNone", "relayInterlockHint", "e_relay_interlocked"):
            if key not in table:
                problems.append(f"the panel has no text {key}")
    if '"voiceUrl"' in app and "192.168.0.180:18090" not in app:
        problems.append("the voice page suggests an address other than the port 18090 where ARMOR-VOICE-AI listens")


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
    check_help_and_frames(table, problems)
    check_panel_features(table, problems)
    if problems:
        print("PANEL_TEXT=FAIL")
        for problem in problems:
            print("  " + problem)
        return 1
    print(f"PANEL_TEXT=PASS {len(table)} rows in {LANGUAGES} languages, {len(codes)} error codes with a text")
    return 0


if __name__ == "__main__":
    sys.exit(main())
