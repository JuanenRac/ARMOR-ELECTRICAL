#!/usr/bin/env python3
"""Check the messages ARMOR-ELECTRICAL's emit_samples prints against ARMOR-COMMON's published contract.

Usage:  emit_samples | python tests/check_samples.py

Each line is a topic and a JSON payload. The firmware's serialiser is hand-written C++; this is the proof that what it prints is what the shared contract
(schema, topic rule, ranges) accepts.
"""
import json
import sys
from pathlib import Path

COMMON = Path(__file__).resolve().parents[2] / "ARMOR-COMMON" / "src"
sys.path.insert(0, str(COMMON))
from armor_common import ContractError, validate_electrical_message  # noqa: E402


def main() -> int:
    checked = 0
    channels = 0
    for line in sys.stdin.read().splitlines():
        topic, _, body = line.partition(" ")
        payload = json.loads(body)
        try:
            validate_electrical_message(topic, payload)
        except ContractError as error:
            print(f"{topic} violates the contract: {error}\n  {body}", file=sys.stderr)
            return 1
        channels += len(payload["channels"])
        checked += 1
    if checked < 3 or channels < 5:
        print(f"expected at least 3 messages with 5 channels, got {checked} and {channels}", file=sys.stderr)
        return 1
    print(f"ELECTRICAL_MESSAGES=PASS {checked} messages accepted by ARMOR-COMMON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
