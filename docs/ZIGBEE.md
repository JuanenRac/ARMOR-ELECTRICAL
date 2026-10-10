# The Zigbee radio: a serial line lent to Zigbee2MQTT

The node does not speak Zigbee. The base board carries a **Zigbee coordinator module** (the proposal is an Ebyte **E72-2G4M20S1E**, a TI CC2652P, 20 dBm, UART) wired to one of the ESP32-S3's serial lines (UART2). The node lends that line over the network, **byte for byte**, to **one client**: Zigbee2MQTT on the server. Zigbee2MQTT runs the Zigbee network (pairing, the devices' descriptions, the commands); ARMOR-SERVER then sees the plugs, breakers, meters, sensors and sirens as ordinary devices through the broker, like any other Zigbee2MQTT installation.

```
 Zigbee devices  --radio-->  E72 module (Z-Stack coordinator)  --UART2-->  node (ESP32-S3)  --TCP-->  Zigbee2MQTT (server)  --MQTT-->  ARMOR-SERVER
```

Nothing in the node interprets what passes: it is a cable. That is deliberate. The node needs no Zigbee stack, no description of any device, and an update of Zigbee2MQTT (new devices, fixes) needs no new firmware here.

## What the node does

- **One client, one address.** The bridge listens on a TCP port (8888 by default) and accepts only the IPv4 address named in the settings (`zigbee.allowed_client`, required while the bridge is on). A connection from any other address is closed at once and counted. A second connection from the allowed address replaces the first (a client that vanished without closing would otherwise hold the radio until the keep-alive noticed). TCP keep-alive and a send timeout stop a dead client from holding the bridge.
- **Whatever the radio says while nobody is connected is dropped** and counted (`dropped`), never buffered.
- **The radio's two control lines.** `reset` drives the radio's RESET_N and `bootloader` its serial-boot pin (DIO15, active low). Both are driven **open-drain**: low, or let go; the node never drives either high. The panel's two buttons use them: *Reset the radio* (RESET_N low for 50 ms, then let go) and *Radio to boot loader* (the boot line low before RESET_N is released, kept low for 150 ms, then both let go). An administrator only; the order and the pauses are in `core/zigbee_bridge.hpp` and tested on a computer.
- **Status** (`GET /api/v1/io`, the *Zigbee radio* page): the state (`starting`, `waiting` for Zigbee2MQTT, `connected`, `error` with `uart`, `pins` or `listen`), the client, the bytes each way, the connections and the refusals.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `zigbee.enabled` | off | Offer the radio on the network. Off: nothing is opened, nothing is checked. |
| `zigbee.rx` / `zigbee.tx` | board profile | The GPIO that receives the radio's TX / sends to the radio's RX. |
| `zigbee.reset` / `zigbee.bootloader` | board profile | The GPIO that drives RESET_N / DIO15. -1: not wired (the matching button disappears). |
| `zigbee.baud` | 115200 | The speed of the radio's firmware: 115200 (a Z-Stack coordinator) or 230400 (the maker's own). |
| `zigbee.port` | 8888 | The TCP port, 1024 to 65535. |
| `zigbee.allowed_client` | empty | The one address that may connect. Required. |

The pins may not be reserved by the board, shared with the meters' line, a relay or an analog input, or each other. The settings apply after a restart of the node.

## Zigbee2MQTT

In `configuration.yaml` of Zigbee2MQTT, on the machine whose address is `allowed_client`:

```yaml
serial:
  port: tcp://<node address>:8888
  adapter: zstack
```

The radio must carry a **Z-Stack coordinator firmware built for the CC2652P** (the P version has an internal power amplifier; a build for the CC2652R or RB is the wrong one). The firmware the module is sold with is the maker's own self-networking program (HEX commands, 230400 baud only): it is **not** the one Zigbee2MQTT expects.

## Giving the radio its firmware

1. Wire the module to the node as in [BASE_BOARD.md](BASE_BOARD.md), turn the bridge on and set `allowed_client` to the address of the computer that will flash (temporarily).
2. On the *Zigbee radio* page press *Radio to boot loader*.
3. From that computer, flash through the bridge with a serial-bootloader tool that can open a TCP socket (`socket://<node address>:8888` in `cc2538-bsl`-style tools), at the baud rate of the module's ROM boot loader.
4. Press *Reset the radio*, set `allowed_client` back to the server and restart Zigbee2MQTT.

Alternatively, flash once with a USB-serial adapter on the module's UART with the boot pin held low at reset.

## What has not been verified

Nothing here has run on a board and no radio has been connected. To confirm with the module in hand and the maker's documents:

- that the module's boot loader is entered with DIO15 low at reset as the maker's manual says, and **that the coordinator firmware keeps that pin as its boot-loader pin** (if it does not, a later reflash needs the module's JTAG pads);
- the Z-Stack build and its settings for this module (UART pins DIO12 and DIO13, the speed, the radio path of the P version);
- that the board's supply holds the radio's transmit peak (the maker gives 106 mA).

## Safety

The radio is the door to every Zigbee device of the house (plugs, breakers, locks, sirens). The allowed address is the only lock the node adds: keep the node and the server on a network you control, and remember that Zigbee2MQTT's own frontend and the broker have their own access rules. See [SAFETY.md](SAFETY.md).
