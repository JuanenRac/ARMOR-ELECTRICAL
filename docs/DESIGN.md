# The electrical node: what it is for and how it is built

ARMOR-ELECTRICAL is the part of A.R.M.O.R. that looks at the house's electrical network: the grid input, the outputs of the solar inverters, the circuits of the distribution board, the DC bus of the batteries. A node measures **channels** (a circuit, a line, a bus) and publishes them; Studio's **Electrical Designer** draws the network and shows, on each element, what its channel measures.

## The pieces

```
 meters (PZEM-004T v3 for AC, PZEM-017 for DC)  --serial, Modbus RTU-->  node (ESP32-S3)  --Wi-Fi, MQTT-->  ARMOR-SERVER  -->  Studio
```

- **`core/pzem.hpp`**: the CRC-16/MODBUS, the request and the decoding of a meter's reply. Read only.
- **`core/pzem_bus.hpp`**: a serial line with several meters (each has its own address): one request at a time, a reply timeout, the last good reading of each, and freshness.
- **`core/electrical_json.hpp`**: the message of the node (see [ELECTRICAL_MESSAGES.md](ELECTRICAL_MESSAGES.md)).
- **`core/interlock.hpp`**: the rules for switching, when a node one day switches something (see [SWITCHING.md](SWITCHING.md) and [SAFETY.md](SAFETY.md)).
- **The firmware** (the ESP32-S3 with its serial line, Wi-Fi, web panel and MQTT), described in [NODE_FIRMWARE.md](NODE_FIRMWARE.md): `core/electrical_config.hpp` (the settings), `main/uart_bus.*` and `main/electrical_manager.*` (the line and the rounds), and the panel of ARMOR-SOLAR's node adapted (Meters and Readings). It reads only; `core/interlock.hpp` is not linked into it.

## Channels and the drawing

A channel of the message has an identifier (`grid`, `heater`, `dc-bus`...). In the Electrical Designer an element is tied to a node and one of its channels (the Node and Channel fields of its properties) and shows its power and voltage; a switch the node sees open is drawn open.

## What is deliberately left out

- Anything that makes a node write to a meter (change its address, its thresholds, reset its energy): meters are set up once, with the maker's tool, before they are installed.
- Any command that reaches a node from the server. The message carries states; the first command will be designed with the hardware in front of it, tried in isolation, and gated by [SWITCHING.md](SWITCHING.md).
