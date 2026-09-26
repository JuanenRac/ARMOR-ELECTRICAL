#!/usr/bin/env python3
"""Check the messages ARMOR-ELECTRICAL's emit_samples prints against ARMOR-COMMON's published contract.

Usage:  emit_samples | python tests/check_samples.py

Each line is a topic and a JSON payload: the state messages (armor/electrical/<node>/state), the commands a server would send to a switch (.../command) and the
results the node answers (.../result). The firmware's serialiser is hand-written C++; this is the proof that what it prints is what the shared contract (schema,
topic rule, ranges, the rules of the whole message) accepts, and that a result answers a command it was given.
"""
import json
import sys
from pathlib import Path

COMMON = Path(__file__).resolve().parents[2] / "ARMOR-COMMON" / "src"
sys.path.insert(0, str(COMMON))
from armor_common import ContractError, validate_electrical_command, validate_electrical_message, validate_electrical_result  # noqa: E402

VALIDATORS = {"state": validate_electrical_message, "command": validate_electrical_command, "result": validate_electrical_result}


def main() -> int:
    counts = {"state": 0, "command": 0, "result": 0}
    channels = 0
    switches = 0
    sent = []
    for line in sys.stdin.read().splitlines():
        topic, _, body = line.partition(" ")
        leaf = topic.rsplit("/", 1)[-1]
        payload = json.loads(body)
        try:
            VALIDATORS[leaf](topic, payload)
        except ContractError as error:
            print(f"{topic} violates the contract: {error}\n  {body}", file=sys.stderr)
            return 1
        counts[leaf] += 1
        if leaf == "state":
            channels += len(payload["channels"])
            switches += len(payload.get("switches", []))
        elif leaf == "command":
            sent.append(payload["command_id"])
        elif payload["command_id"] not in sent:
            print(f"a result answers a command that was never sent: {body}", file=sys.stderr)
            return 1
    if counts["state"] < 3 or channels < 5 or switches < 4 or counts["command"] < 8 or counts["result"] < 8:
        print(f"expected states with 5 channels and 4 switches and 8 commands answered, got {counts}, {channels} channels, {switches} switches", file=sys.stderr)
        return 1
    print(f"ELECTRICAL_MESSAGES=PASS {counts['state']} states, {counts['command']} commands and {counts['result']} results accepted by ARMOR-COMMON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
