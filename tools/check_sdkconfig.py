#!/usr/bin/env python3
# ARMOR-NODE-ELECTRICAL - says whether the sdkconfig a build produced really carries what sdkconfig.defaults asks for.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#
# ESP-IDF reads sdkconfig.defaults only for the symbols that its generated sdkconfig does not have yet: a build directory (.compile/<profile>/sdkconfig, build/<profile>/sdkconfig) made before a
# line was added or changed keeps the old value, and nothing says so. Run this after a build (or before: it reads what is there):
#
#     python tools/check_sdkconfig.py                 # every generated sdkconfig under .compile/ and build/
#     python tools/check_sdkconfig.py path/to/sdkconfig
#     python tools/check_sdkconfig.py --self-test
#
# Exit 0 when every line of sdkconfig.defaults (and of sdkconfig.board.<board> of the profile) holds in the generated file; 1 with the lines that differ (delete that build directory and build
# again); 2 when there is nothing to check. A symbol that the generated file does not mention at all is reported too: the Kconfig that defines it was not part of that build, or it was dropped.
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SET = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
UNSET = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")


def wanted(path: Path) -> dict[str, str]:
    """The symbols a defaults file asks for: value as written (`n` is kept as `n`)."""
    found: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        match = SET.match(line)
        if match:
            found[match.group(1)] = match.group(2).strip()
    return found


def generated(path: Path) -> dict[str, str]:
    """The symbols of a generated sdkconfig: the value, or `n` for "is not set"."""
    found: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        match = SET.match(line)
        if match:
            found[match.group(1)] = match.group(2).strip()
            continue
        match = UNSET.match(line)
        if match:
            found[match.group(1)] = "n"
    return found


def differences(asked: dict[str, str], made: dict[str, str]) -> list[str]:
    out = []
    for symbol, value in sorted(asked.items()):
        if symbol not in made:
            if value != "n":
                out.append(f"{symbol}={value}: not in the generated file")
        elif made[symbol] != value:
            out.append(f"{symbol}: asked {value}, generated {made[symbol]}")
    return out


def profile_of(sdkconfig: Path) -> str:
    """`publica-s3-eth` -> `s3-eth`; `generic-s3-wifi` -> `s3-wifi`; anything else -> ''."""
    match = re.search(r"(s3-eth|s3-wifi)$", sdkconfig.parent.name)
    return match.group(1) if match else ""


def check(sdkconfig: Path) -> list[str]:
    asked = wanted(ROOT / "sdkconfig.defaults")
    board = profile_of(sdkconfig)
    if board and (ROOT / f"sdkconfig.board.{board}").exists():
        asked.update(wanted(ROOT / f"sdkconfig.board.{board}"))
    return differences(asked, generated(sdkconfig))


def self_test() -> int:
    asked = {"CONFIG_A": "y", "CONFIG_B": "n", "CONFIG_C": "4096", "CONFIG_D": "y"}
    made = {"CONFIG_A": "y", "CONFIG_B": "n", "CONFIG_C": "16384"}
    got = differences(asked, made)
    assert got == ["CONFIG_C: asked 4096, generated 16384", "CONFIG_D=y: not in the generated file"], got
    assert differences({"CONFIG_X": "n"}, {}) == []   # an unset symbol that the build never heard of is the same thing
    assert profile_of(Path("x/.compile/publica-s3-eth/sdkconfig")) == "s3-eth"
    assert profile_of(Path("x/build/foo/sdkconfig")) == ""
    print("check_sdkconfig self-test passed")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()
    paths = [Path(a) for a in argv if not a.startswith("--")]
    if not paths:
        for folder in (".compile", "build"):
            paths += sorted((ROOT / folder).glob("*/sdkconfig")) if (ROOT / folder).is_dir() else []
    if not paths:
        print("no generated sdkconfig found (build first, or give its path)")
        return 2
    failed = False
    for path in paths:
        problems = check(path)
        if problems:
            failed = True
            print(f"{path}: {len(problems)} line(s) of sdkconfig.defaults are not what was built (delete this build directory and build again):")
            for problem in problems:
                print("  " + problem)
        else:
            print(f"{path}: every line of sdkconfig.defaults holds")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
