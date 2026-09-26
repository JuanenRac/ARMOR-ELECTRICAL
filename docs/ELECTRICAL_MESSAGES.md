# The messages of an electrical node (contract version 0)

`armor/electrical/{node_id}/state`, one message per node, every few seconds; JSON, no retained flag. The schema, the topic rule and the conformance vectors are in ARMOR-COMMON (`electrical.schema.json`, `conformance/electrical.json`); the server takes the message over MQTT or `POST /api/v1/electrical/readings` and gives an operator `GET /api/v1/electrical/readings` and `GET /api/v1/electrical/history`.

```json
{"kind":"electrical","node_id":"electrical-1","timestamp_ms":1000,"switching_enabled":false,
 "channels":[{"id":"grid","domain":"ac","label":"Grid input","voltage_v":230.5,"current_a":1.234,"power_w":284.4,"energy_kwh":1.5,"frequency_hz":50.0,"power_factor":0.98,"alarm":false}]}
```

- Up to sixteen channels; an identifier appears once. `domain` is `ac` or `dc`. `power_w` is positive when the channel draws power from the network and negative when it feeds it. `energy_kwh` is what has passed since the meter was last reset.
- `state` (`closed`, `open`, `unknown`) is for a channel with a switch and is what the node **sees** (the auxiliary contact), never what it was asked.
- `switching_enabled` says whether the node's firmware may switch anything at all; it is false unless it was built and set up for it.
- The message carries states, never a command; a field that is not in the schema is refused.
- A node silent for a minute is stale on the server and counts for nothing in its sums.
