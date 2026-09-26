# The firmware of the electrical node

The firmware of an **ESP32-S3** node (the same two boards and the same board profiles as ARMOR-SOLAR's node: `s3-wifi`, an ESP32-S3-WROOM-1 N16R8 whose way in is Wi-Fi, and `s3-eth`, the Waveshare ESP32-S3-ETH whose way in is the cable). It reads up to **sixteen energy meters** (PZEM-004T v3 for AC, PZEM-017 for DC) on **one serial line** and publishes their readings to A.R.M.O.R.'s broker (`armor/electrical/<node>/state`, the message of ARMOR-COMMON). It has the same web panel as the other nodes (setup, login, users, Wi-Fi, broker, over-the-air update with rollback, log, the panel over HTTPS) and two pages of its own: **Meters** and **Readings**.

**It only reads.** The firmware never builds a request that writes to a meter, and it does not drive any contactor, relay or breaker: the rules for switching (`core/interlock.hpp`) are in the repository, tested, and **not linked into the firmware**.

**Nothing here has run on a board and no meter has been connected.** What has been done: the settings, the meters' frames and the bus are tested on a computer with stand-in meters, the messages the node makes are accepted by ARMOR-COMMON, and the firmware is built in the ESP-IDF container (see the CHANGELOG for the result).

## The serial line and the meters

One hardware UART carries the line. Set, in the panel's **Meters** page:

- the **RX** and **TX** pins (the defaults are GPIO 16 and 15), the **speed** (the PZEM meters talk 9600 baud, 8N1) and the **seconds between two rounds**;
- for each of up to sixteen meters: whether it is read, the **channel** it makes in the message (lowercase letters, digits, `-` and `_`; unique), an optional name, its **kind** (`ac` for a PZEM-004T v3, `dc` for a PZEM-017) and its **Modbus address** (1 to 247, unique on the line).

The meters' addresses are set once with the maker's tool, one meter at a time, **before** they are installed on the shared line: the node does not change them. The serial side of a PZEM meter is 5 V: it needs a level adaptation to the 3.3 V of the board (see [HARDWARE.md](HARDWARE.md)). Changes to the line and the meters apply after a restart of the node.

A round asks each enabled meter in turn, one request at a time, with a reply timeout. The page shows, for each meter, its state (`reading`, `silent`: no answer, `garbled`: bytes but no valid reply, `waiting`, `disabled`) and its counters of good, bad and lost replies, with a hint for the usual causes (address, crossed TX and RX, ground, level adaptation, speed, two meters with one address).

## The message

Each round makes one message with a channel per meter that answered recently; a meter silent for ten seconds is left out rather than repeated. The node publishes it on `armor/electrical/<node>/state` (QoS 0, not retained: the server keeps the latest reading, with the node's own identity on the broker; see ARMOR-DEVOPS `scripts/mqtt_identity.sh add electrical-node <id>`, which allows writing under `armor/electrical/<node>/#` and reading nothing). The **Readings** page shows the last message channel by channel.

## Build

`tools/build_node.sh generic` writes the universal image `dist/generic-s3-wifi.bin`, and `tools/build_node.sh generic s3-eth` the one for the Ethernet board (an image is for ONE board). Run it from Linux or from WSL on Windows (Docker needed); one build at a time. The panel is packed into the firmware by `tools/pack_panel.py`.

## At the bench (when the hardware is there)

1. Flash the image, open the set-up network, choose the administrator with the code the node shows on its USB console.
2. Join the node to the Wi-Fi and to the broker (the identity above).
3. Wire **one** meter first, powered from a supply that is safe to have on a bench, with its serial side adapted to 3.3 V; set its channel and address and check that the page says `reading` and the numbers agree with a reference instrument.
4. Add the meters one by one, each with its own address; never two with the same address on the line.
5. In Studio, open the Electrical Designer, tie an element to the node and the channel, and check the numbers on the drawing.
