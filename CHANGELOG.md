# Changelog

All notable changes to this project are documented here.

## [0.0.4] - The commands to a switch, tested and not linked

- **`core/switch_set.hpp`:** the layer between a message and the transfer controller. It reads a command of ARMOR-COMMON's new `electrical_command` schema strictly (`arm`, `close_a`, `close_b`, `open`, `acknowledge`), refuses what it should (`disabled`, `fault`, `not_armed`, `not_confirmed_open`, `unknown_switch`, `bad_token`, `not_supported`) and answers with an `electrical_result`. Closing is two steps: an accepted `arm` gives a one-time 64-bit token and a close must carry it (a wrong token withdraws the arm; the token is spent by the first close); `open` needs neither and always works. The state message can carry `switches` (`SwitchSet::switches_json`, `message_json` takes the array).
- `TransferController::disarm()` (withdraws an unused arm; never moves anything).
- `tests/test_switch_set.cpp` (about 240,000 checks): the reader against the shared vectors, the two-step close and its replays, every refusal, a welded contactor, the state message, and 40 rounds of random commands and noise against contactors that weld and stick; a mutation that skips the token check is caught. `tests/emit_samples.cpp` prints states, commands and answers and `check_samples.py` has ARMOR-COMMON validate them (and that an answer answers a command that was sent).
- **Not linked into the firmware:** no image creates a `SwitchSet`, the state message still says `switching_enabled: false` and has no `switches`. Nothing here has driven a contactor.

## [0.0.3] - Configuration from a phone over Bluetooth, and the meters loop tested

- **Bluetooth Low Energy (NimBLE), the same channel as the other nodes:** a phone running the ARMOR app finds the node as `ARMOR-XXXXXX` and sets up its name, Wi-Fi station, address, broker and the rest, with the same set-up code and the same users as the panel (`docs/BLE_PROVISIONING.md`). New setting `ble.mode` (`setup` by default, `always`, `off`) in the panel's Network page, in seven languages. 68 host checks of the framing, the requests, the access rules and the setting.
- **What the node does on every turn of its loop is now tested on a computer** (`core/meter_runner.hpp`, 62 checks): the firmware's task only opens the UART and gives it the clock. A stand-in line plays the meters: one that reads, a DC one, one that does not answer, one with a bad CRC, another address or a refusal, replies in pieces, replies that come late, a line that will not send, sixteen meters, the message's clock and the polling period.
- **A fix the tests found:** a meter that worked and then stopped answering was shown as *waiting* for ever; it is now *silent* (or *garbled*), with the reason, and it leaves the message after ten seconds as before.
- **The panel's rows** no longer stretch to the tallest field of a row (the same one-line change in the three node projects, kept once in ARMOR-COMMON's `firmware_base`).
- **The panel** was exercised in a real browser (headless Edge) against `tools/panel_mock.mjs`, a stand-in node, on both board profiles: set-up, login, the Meters and Readings pages, saving with a refused and a corrected meter, the Bluetooth setting, every page in the seven languages, the width of a phone. The texts of the solar node that this panel no longer uses were removed.
- **Not done:** the radio side has never run on a board and no phone has talked to it; nothing has run on a board.

## [0.0.2] - The firmware of the electrical node

- **The firmware,** forked from the solar node's and reduced to what a node of meters needs: the settings stored in flash (`core/electrical_config.hpp`), **one serial line** (a hardware UART, 9600 baud by default) for up to **sixteen meters** (PZEM-004T v3 AC or PZEM-017 DC, each with its channel, name, kind and Modbus address), the rounds that ask them one at a time, and the message published on `armor/electrical/<node>/state` (QoS 0, not retained). It **only reads**: no request that writes to a meter exists, no output pin is driven, and the rules for switching are not linked into it.
- **The panel** is the other nodes' (set-up with the code on the USB console, users, Wi-Fi station and access point, broker, update over the air with rollback, log, HTTPS) with two pages of its own, **Meters** (the line and each meter, with its state, its counters and a hint for the usual causes of silence) and **Readings** (the last message, channel by channel), in seven languages.
- **Two boards, one code base:** the ESP32-S3-WROOM-1 N16R8 on Wi-Fi (`s3-wifi`) and the Waveshare ESP32-S3-ETH on its cable (`s3-eth`); the universal images build in the ESP-IDF 5.4.2 container without warnings.
- **Tests:** 71 checks of the settings (defaults, the meters' channels and addresses, the pins of the line, the stored document and what the panel may send) on top of the 53 and 135,736 of 0.0.1.
- **Not done:** running on a board, any meter or contactor connected, any measurement of a real installation, any switching.

## [0.0.1] - The core of the electrical node

- **Reading the meters.** The frames of the Peacefair **PZEM-004T v3** (AC) and **PZEM-017** (DC) over Modbus RTU: the CRC-16, the request that reads the input registers and the decoding of the reply (voltage, current, power, energy, frequency, power factor, alarm). Only the read request is built; the requests that write to a meter (its address, its thresholds, the reset of its energy counter) are not built anywhere. A reply is refused when its CRC, address, function or length do not fit, when it is an exception, or when it holds a value no real meter could give.
- **The bus.** A line with several meters is asked one at a time, in order, with a reply timeout; the last good reading of each is kept, a meter that goes silent is counted and its old reading is not published after ten seconds.
- **The message.** `armor/electrical/{node}/state` (kind `electrical`, one entry per channel: AC or DC, voltage, current, power, energy, and for AC the frequency and the power factor; the state of a switch as the node sees it; an alarm), which the shared contract of ARMOR-COMMON accepts. It carries a state and never a command.
- **The rules for switching**, kept apart from any hardware: a controller for the transfer between two sources that never commands both, needs the switching allowed, the node armed just before, both contactors confirmed open for the whole dead time and a fault-free state, treats a contactor that does not show what it was told as a fault that stays until it is acknowledged with both confirmed open, and always opens at once when asked. **Switching is off by default and nothing drives any hardware.**
- **Tests:** 53 checks of the meters and 135,736 of the switching rules (scripted scenarios and random steps with contactors that weld, stick and fall out), and the messages the node makes are checked against ARMOR-COMMON.
- **Not done:** the firmware of the node, every piece of hardware, any measurement of a real installation, any switching.
