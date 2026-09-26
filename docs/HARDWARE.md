# The hardware: what is proposed and what is not decided

Nothing here has been built or connected.

- **Node:** an ESP32-S3 (the ESP32-S3-WROOM-1 N16R8 on Wi-Fi, as the solar nodes use), inside the electrical panel, on its own low-voltage supply.
- **AC meters:** PZEM-004T v3 modules (100 A with a split-core transformer, 80 to 260 V), one per measured circuit or line, each with its own serial address. Their serial side is optically isolated; its level is 5 V.
- **DC meters:** PZEM-017 modules (300 V, an external shunt) on the battery bus and the PV lines.
- **The serial line:** the meters' serial links can share one UART of the ESP32-S3 (each meter answers only its own address); the 5 V side needs a level adaptation to 3.3 V, and the line is short and inside the panel.
- **Switching (later):** contactors or electronic breakers with an auxiliary contact whose state the node reads, a mechanical interlock between the two contactors of a source transfer, and coils driven through isolated drivers. See [SAFETY.md](SAFETY.md) and [SWITCHING.md](SWITCHING.md).

## The serial line and its levels (a proposal, untried)

- **Pins:** the firmware's defaults are GPIO 16 for the line's RX (it receives what the meters send) and GPIO 15 for its TX; both are settings in the panel's *Meters* page and any free pin of the board may be used.
- **Wiring:** each PZEM module has a four-pin serial header (5 V, TX, RX, GND) whose side is isolated from the mains side; the node supplies that 5 V. The node's TX goes to every module's RX, and every module's TX to the node's RX.
- **Levels:** the modules' TX is a 5 V level and the ESP32-S3's inputs are 3.3 V. The safe way is a level adaptation on the node's RX line: a resistor divider (for example 10 kΩ and 20 kΩ) or a small bidirectional level shifter. The node's 3.3 V TX into the module's RX is what many projects do; check it on the bench.
- **Several modules on one line:** their TX outputs share the wire, so how they behave together, and whether a diode or a buffer per module is needed, is checked on the bench with two modules before any more are added. One address each, set before installing (see [PROTOCOLS.md](PROTOCOLS.md)).
- **Speed:** 9600 baud, 8N1, as the modules speak; a round of sixteen meters takes a few seconds, and *Seconds between rounds* in the panel must leave room for it.

## To decide

- Which circuits are measured (the grid input, each inverter's output, the water heater, the sockets, the DC buses) and how many meters, hence how many nodes.
- Whether the direction of the power on the grid input is needed (a meter that measures it).
- Where each node lives, what supplies it, and what it does when the network is down.
