# The messages of an electrical node (contract version 0)

`armor/electrical/{node_id}/state`, one message per node, every few seconds; JSON, no retained flag. The schema, the topic rule and the conformance vectors are in ARMOR-COMMON (`electrical.schema.json`, `conformance/electrical.json`); the server takes the message over MQTT or `POST /api/v1/electrical/readings` and gives an operator `GET /api/v1/electrical/readings` and `GET /api/v1/electrical/history`.

```json
{"kind":"electrical","node_id":"electrical-1","timestamp_ms":1000,"switching_enabled":false,
 "channels":[{"id":"grid","domain":"ac","label":"Grid input","voltage_v":230.5,"current_a":1.234,"power_w":284.4,"energy_kwh":1.5,"frequency_hz":50.0,"power_factor":0.98,"alarm":false}]}
```

- Up to sixteen channels; an identifier appears once. `domain` is `ac` or `dc`. `power_w` is positive when the channel draws power from the network and negative when it feeds it. `energy_kwh` is what has passed since the meter was last reset.
- **Analog channels are estimates.** A channel made from a current input carries `"power_estimated":true`, so a consumer shows its power and energy as an estimate and does not add them to measured totals without saying so (the server marks a grid figure that includes one as estimated). For a current input the node publishes `power_w` as the current times the nominal voltage set for the input (an apparent power, not the active power of an inductive load), and its `energy_kwh` is counted by the node since it started: it begins again at zero at every restart, which a consumer must read as a reset. A PZEM-004T gives no direction of the power; the sign is positive.
- `state` (`closed`, `open`, `unknown`) is for a channel with a switch and is what the node **sees** (the auxiliary contact), never what it was asked.
- `switching_enabled` says whether the node's firmware may switch anything at all; it is false unless it was built and set up for it.
- The state message carries states, never a command; a field that is not in the schema is refused.
- **`switches`** (optional, up to four): the state of the node's switches, from its auxiliary contacts (`a_closed`, `b_closed`, `selected`, `wanted`, `closing`, `armed`, `fault`); see [SWITCHING.md](SWITCHING.md).

## The command and the answer

`armor/electrical/{node_id}/command` (server → node) and `armor/electrical/{node_id}/result` (node → server), never retained; the schemas and vectors are in ARMOR-COMMON (`electrical_command.schema.json`, `electrical_result.schema.json`).

```json
{"kind":"electrical_command","node_id":"electrical-1","timestamp_ms":7000,"command_id":"c0ffee0123456789","switch":"transfer","action":"arm"}
{"kind":"electrical_result","node_id":"electrical-1","timestamp_ms":7100,"command_id":"c0ffee0123456789","switch":"transfer","action":"arm","accepted":true,"refusal":"none","token":"031425364758697a"}
{"kind":"electrical_command","node_id":"electrical-1","timestamp_ms":8000,"command_id":"c0ffee0123456790","switch":"transfer","action":"close_a","token":"031425364758697a"}
```

- The actions are `arm`, `close_a`, `close_b`, `open` and `acknowledge`; a `token` goes on the two closes and on nothing else. The answer's `refusal` is `none` exactly when it accepted, and its `token` is in the answer to an accepted `arm` only.
- An accepted command is **not** a switch that closed: only the `state` message says that.
- A node silent for a minute is stale on the server and counts for nothing in its sums.
