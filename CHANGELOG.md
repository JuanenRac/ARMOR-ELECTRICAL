# Changelog

All notable changes to this project are documented here.

## [0.0.1] - The core of the electrical node

- **Reading the meters.** The frames of the Peacefair **PZEM-004T v3** (AC) and **PZEM-017** (DC) over Modbus RTU: the CRC-16, the request that reads the input registers and the decoding of the reply (voltage, current, power, energy, frequency, power factor, alarm). Only the read request is built; the requests that write to a meter (its address, its thresholds, the reset of its energy counter) are not built anywhere. A reply is refused when its CRC, address, function or length do not fit, when it is an exception, or when it holds a value no real meter could give.
- **The bus.** A line with several meters is asked one at a time, in order, with a reply timeout; the last good reading of each is kept, a meter that goes silent is counted and its old reading is not published after ten seconds.
- **The message.** `armor/electrical/{node}/state` (kind `electrical`, one entry per channel: AC or DC, voltage, current, power, energy, and for AC the frequency and the power factor; the state of a switch as the node sees it; an alarm), which the shared contract of ARMOR-COMMON accepts. It carries a state and never a command.
- **The rules for switching**, kept apart from any hardware: a controller for the transfer between two sources that never commands both, needs the switching allowed, the node armed just before, both contactors confirmed open for the whole dead time and a fault-free state, treats a contactor that does not show what it was told as a fault that stays until it is acknowledged with both confirmed open, and always opens at once when asked. **Switching is off by default and nothing drives any hardware.**
- **Tests:** 53 checks of the meters and 135,736 of the switching rules (scripted scenarios and random steps with contactors that weld, stick and fall out), and the messages the node makes are checked against ARMOR-COMMON.
- **Not done:** the firmware of the node (Wi-Fi, panel, MQTT, the serial ports), every piece of hardware, any measurement of a real installation, any switching.
