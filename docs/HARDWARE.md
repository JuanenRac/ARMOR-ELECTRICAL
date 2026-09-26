# The hardware: what is proposed and what is not decided

Nothing here has been built or connected.

- **Node:** an ESP32-S3 (the ESP32-S3-WROOM-1 N16R8 on Wi-Fi, as the solar nodes use), inside the electrical panel, on its own low-voltage supply.
- **AC meters:** PZEM-004T v3 modules (100 A with a split-core transformer, 80 to 260 V), one per measured circuit or line, each with its own serial address. Their serial side is optically isolated; its level is 5 V.
- **DC meters:** PZEM-017 modules (300 V, an external shunt) on the battery bus and the PV lines.
- **The serial line:** the meters' serial links can share one UART of the ESP32-S3 (each meter answers only its own address); the 5 V side needs a level adaptation to 3.3 V, and the line is short and inside the panel.
- **Switching (later):** contactors or electronic breakers with an auxiliary contact whose state the node reads, a mechanical interlock between the two contactors of a source transfer, and coils driven through isolated drivers. See [SAFETY.md](SAFETY.md) and [SWITCHING.md](SWITCHING.md).

## To decide

- Which circuits are measured (the grid input, each inverter's output, the water heater, the sockets, the DC buses) and how many meters, hence how many nodes.
- Whether the direction of the power on the grid input is needed (a meter that measures it).
- Where each node lives, what supplies it, and what it does when the network is down.
