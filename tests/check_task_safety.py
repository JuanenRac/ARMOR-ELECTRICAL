#!/usr/bin/env python3
# ARMOR-NODE-ELECTRICAL - guard against the stack overflows that only show on a board: reads main/*.cpp (nothing is compiled; the ESP-IDF headers are not needed) and fails when
#   1. a FreeRTOS task is created with a stack below the minimum, or below the one its job needs (the table of tasks is printed with --table);
#   2. a function a task can reach (the call graph is followed by name, so it may over-reach) keeps a whole config::Settings, or a buffer of more than 1 KB, on its stack;
#   3. the HTTP client that fetches the release from GitHub has a header buffer below what GitHub's redirects need (about 5.2 KB), or sets a size that is not header_buffer().
# A copy that is needed and safe is allowed with a comment `// task-stack-ok: <reason>` on its line or the line before.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MIN_STACK = 3072                       # no task of the node has less
BUFFER_LIMIT = 1024                    # a local array above this goes to the heap
# What a task needs at least, by its name. Every number is the size it has now or more: shrinking one has to be a decision, not an accident.
NEEDS = {
    "telemetry": (6144, "builds and publishes the whole telemetry JSON and the settings"),
    "heartbeat": (4096, "builds the node info JSON"),
    "mqtt-link": (6144, "TLS and the MQTT client"),
    "mqtt-out": (4096, "queue to the broker"),
    "github-check": (12288, "TLS and the release JSON"),
    "github-ota": (12288, "TLS, the download and the signature check"),
    "slot-copy": (6144, "copies a firmware slot"),
    "ble-worker": (16384, "NimBLE requests that apply a whole settings document"),
    "radar": (6144, "parses the sensor frames"),
}
SAFE = ("task-stack-ok:",)
KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else", "do", "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast", "decltype", "alignof", "defined"}


def strip(text: str) -> str:
    """Comments and string contents out (lengths kept for line numbers), except the task-stack-ok marks."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            segment = text[i:j]
            out.append(segment if any(mark in segment for mark in SAFE) else " " * len(segment))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            keep = text[i:j + 1] if c == '"' else " " * (j + 1 - i)
            out.append(keep)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def match_brace(text: str, start: int) -> int:
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


DEFINITION = re.compile(r"(?<![.>\w:])((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[\w:<>\s*&]+)?\{")


def functions(text: str) -> list[tuple[str, int, int]]:
    """(name, body start, body end) for every function definition found."""
    found = []
    for m in DEFINITION.finditer(text):
        name = m.group(1).split("::")[-1]
        if name in KEYWORDS:
            continue
        start = m.end() - 1
        found.append((name, start, match_brace(text, start)))
    return found


TASK_CALL = re.compile(r"\b(xTaskCreate\w*|start_task)\s*\(\s*(?:&)?(?:[\w:]+::)?(\w+)\s*,\s*\"([^\"]+)\"\s*,\s*([^,]+),")
CONSTANT = re.compile(r"constexpr\s+(?:unsigned|int|std::size_t|size_t|std::uint32_t|uint32_t)\s+(\w+)\s*=\s*([\d' *<]+);")


def value_of(expression: str, constants: dict[str, int]) -> int | None:
    expression = expression.strip().replace("'", "")
    if expression in constants:
        return constants[expression]
    if re.fullmatch(r"[\d\s*+<]+", expression):
        try:
            return int(eval(expression, {"__builtins__": {}}, {}))   # digits, spaces, * + << only
        except Exception:   # noqa: BLE001
            return None
    return None


def load(root: Path) -> dict[Path, str]:
    return {p: strip(p.read_text(encoding="utf-8", errors="replace")) for p in sorted((root / "main").glob("*.cpp"))}


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


SETTINGS_BYTES = 1100                  # sizeof(config::Settings) on the board (about 1.36 KB on a 64-bit host)
NAMESPACE = re.compile(r"namespace\s+(?:armor::)?(\w+)(?:::(\w+))?\s*\{")


def namespace_of(text: str) -> str:
    for m in NAMESPACE.finditer(text):
        last = m.group(2) or m.group(1)
        if last != "armor":
            return last
    return ""


def analyse(root: Path) -> tuple[list[tuple[str, int, str, str]], list[str]]:
    """(tasks: [(name, stack, function, file)], problems)."""
    sources = load(root)
    problems: list[str] = []
    tasks: list[tuple[str, int, str, str]] = []
    spaces = {path: namespace_of(text) for path, text in sources.items()}
    by_space: dict[str, list[Path]] = {}
    for path, space in spaces.items():
        by_space.setdefault(space or path.stem, []).append(path)
    bodies: dict[tuple[Path, str], list[tuple[int, int]]] = {}
    constants: dict[str, int] = {}
    for path, text in sources.items():
        for m in CONSTANT.finditer(text):
            try:
                constants[m.group(1)] = int(eval(m.group(2).replace("'", ""), {"__builtins__": {}}, {}))
            except Exception:   # noqa: BLE001
                pass
        for name, a, b in functions(text):
            bodies.setdefault((path, name), []).append((a, b))
    for path, text in sources.items():
        for m in TASK_CALL.finditer(text):
            function, name, size_text = m.group(2), m.group(3), m.group(4)
            size = value_of(size_text, constants)
            where = f"{path.name}:{line_of(text, m.start())}"
            if size is None:
                problems.append(f"{where}: the stack of task '{name}' is not a number ({size_text.strip()!r}): write it as a literal so it can be checked")
                continue
            tasks.append((name, size, function, path.name))
            need, why = NEEDS.get(name, (MIN_STACK, "default minimum"))
            if size < max(need, MIN_STACK):
                problems.append(f"{where}: task '{name}' has a {size}-byte stack, below the {max(need, MIN_STACK)} it needs ({why})")
        # httpd serves the panel from its own task: its stack is set in the server configuration
        for m in re.finditer(r"stack_size\s*=\s*(\d+)", text):
            if int(m.group(1)) < MIN_STACK:
                problems.append(f"{path.name}:{line_of(text, m.start())}: a stack of {m.group(1)} bytes is below {MIN_STACK}")
    paths_by_name = {path.name: path for path in sources}
    for name, size, function, filename in tasks:
        start = (paths_by_name[filename], function)
        seen: set[tuple[Path, str]] = set()
        todo = [start]
        while todo:
            current = todo.pop()
            if current in seen or current not in bodies:
                continue
            seen.add(current)
            path, _ = current
            for a, b in bodies[current]:
                body = sources[path][a:b]
                for call in re.finditer(r"(?<![.>\w])((?:[A-Za-z_]\w*::)*)([A-Za-z_]\w*)\s*\(", body):
                    callee, qualifier = call.group(2), call.group(1)
                    if callee in KEYWORDS:
                        continue
                    if qualifier:
                        for other in by_space.get(qualifier.rstrip(":").split("::")[-1], []):
                            todo.append((other, callee))
                    else:
                        todo.append((path, callee))
        contributions: list[tuple[int, str]] = []
        for path, reached in sorted(seen, key=lambda item: (item[0].name, item[1])):
            text = sources[path]
            for a, b in bodies[(path, reached)]:
                body = text[a:b]
                signature = text[max(0, text.rfind(reached + "(", 0, a)):a]
                for m in re.finditer(r"[(,]\s*(?:const\s+)?config::Settings\s+\w+\s*(?=[,)])", signature):
                    number = line_of(text, a)
                    context = chr(10).join(text.split(chr(10))[max(0, number - 3):number])
                    if not any(mark in context for mark in SAFE):
                        contributions.append((SETTINGS_BYTES, f"{path.name}:{number} {reached}() takes a config::Settings by value"))
                for pattern, kind in ((r"\bconfig::Settings\s+\w+\s*(?:=|;|\{|\()", "settings"),
                                      (r"\b(?:char|uint8_t|unsigned char|int16_t|int32_t|float)\s+\w+\s*\[\s*(\d+)\s*\]", "array")):
                    for m in re.finditer(pattern, body):
                        if kind == "array":
                            weight = int(m.group(1)) * (1 if re.match(r"(?:char|uint8_t|unsigned char)", m.group(0)) else 4)
                            if weight <= BUFFER_LIMIT:
                                continue
                        else:
                            weight = SETTINGS_BYTES
                        number = line_of(text, a + m.start())
                        context = chr(10).join(text.split(chr(10))[max(0, number - 2):number])
                        if any(mark in context for mark in SAFE):
                            continue
                        contributions.append((weight, f"{path.name}:{number} {reached}() {'a config::Settings copy' if kind == 'settings' else 'a ' + str(weight) + '-byte array'}"))
        total = sum(weight for weight, _ in contributions)
        if contributions and total * 2 > size:
            problems.append(f"task '{name}' ({size} bytes) can reach {total} bytes of big locals, more than half its stack: " + "; ".join(text for _, text in contributions)
                            + ". Put them on the heap (std::unique_ptr / new (std::nothrow)) or mark each `// task-stack-ok: <reason>`")
    problems += http_buffer_problems(root, sources)
    seen_problems: list[str] = []
    for p in problems:
        if p not in seen_problems:
            seen_problems.append(p)
    return tasks, seen_problems


def found_configs(text):
    return list(re.finditer(r"buffer_size\s*=\s*[^;]+;", text))


HEADER_BUFFER_MIN = 6144               # the redirect of a GitHub download carries about 5.2 KB of headers


def http_buffer_problems(root: Path, sources: dict[Path, str]) -> list[str]:
    problems = []
    updater = [(p, t) for p, t in sources.items() if p.name == "github_update.cpp"]
    for path, text in updater:
        found = list(re.finditer(r"\bbuffer_size\s*=\s*([^;]+);", text))
        if not found:
            problems.append(f"{path.name}: no HTTP client sets buffer_size: GitHub's redirect headers (about 5.2 KB) do not fit the 4 KB default")
        for m in found:
            expression = m.group(1).strip()
            number = re.fullmatch(r"\d+", expression)
            if "header_buffer()" not in expression and not (number and int(expression) >= HEADER_BUFFER_MIN):
                problems.append(f"{path.name}:{line_of(text, m.start())}: buffer_size = {expression} is not header_buffer() nor a number of {HEADER_BUFFER_MIN} or more")
    for path, text in updater:
        # the REQUEST buffer too: after GitHub's redirect the request line carries an address of about a thousand characters, and the client's default of 512 bytes says "Out of buffer"
        tx = list(re.finditer(r"buffer_size_tx\s*=\s*([^;]+);", text))
        if len(tx) < len(found_configs(text)):
            problems.append(f"{path.name}: an HTTP client does not set buffer_size_tx (the default 512 bytes cannot hold GitHub's redirect address)")
        for m in tx:
            expression = m.group(1).strip()
            number = re.fullmatch(r"\d+", expression)
            if "kRequestBuffer" not in expression and not (number and int(expression) >= 2048):
                problems.append(f"{path.name}:{line_of(text, m.start())}: buffer_size_tx = {expression} is not release::kRequestBuffer nor a number of 2048 or more")
    release = root / "main" / "core" / "release_assets.hpp"
    if updater and release.is_file():
        text = release.read_text(encoding="utf-8")
        for name in ("kHeaderBufferInternal", "kHeaderBufferExternal"):
            m = re.search(rf"{name}\s*=\s*(\d+)", text)
            if not m or int(m.group(1)) < HEADER_BUFFER_MIN:
                problems.append(f"release_assets.hpp: {name} is below {HEADER_BUFFER_MIN} bytes: a GitHub redirect would not fit")
    return problems


def main(argv: list[str]) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 and not argv[1].startswith("--") else ROOT
    tasks, problems = analyse(root)
    if "--table" in argv:
        for name, size, function, filename in sorted(tasks, key=lambda t: (-t[1], t[0])):
            print(f"{size:6d}  {name:<14} {function:<24} {filename}")
    for line in problems:
        print("TASK_SAFETY=FAIL " + line)
    if not tasks:
        print("TASK_SAFETY=FAIL no task found: the scan is broken")
        return 1
    if problems:
        return 1
    print(f"TASK_SAFETY=PASS {len(tasks)} tasks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
