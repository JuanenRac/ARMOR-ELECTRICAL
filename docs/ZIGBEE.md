# The Zigbee radio: a serial line lent to Zigbee2MQTT

The node does not speak Zigbee. The base board carries a **Zigbee coordinator module** (a TI CC2652P7 on an RF-star **RF-BM-2652P4** for the first boards, or an Ebyte **E72-2G4M20S1E** with a CC2652P, 20 dBm, UART; see *Radio modules that work with this bridge*) wired to one of the ESP32-S3's serial lines (UART2). The node lends that line over the network, **byte for byte**, to **one client**: Zigbee2MQTT on the server. Zigbee2MQTT runs the Zigbee network (pairing, the devices' descriptions, the commands); ARMOR-SERVER then sees the plugs, breakers, meters, sensors and sirens as ordinary devices through the broker, like any other Zigbee2MQTT installation.

```
 Zigbee devices  --radio-->  E72 module (Z-Stack coordinator)  --UART2-->  node (ESP32-S3)  --TCP-->  Zigbee2MQTT (server)  --MQTT-->  ARMOR-SERVER
```

Nothing in the node interprets what passes: it is a cable. That is deliberate. The node needs no Zigbee stack, no description of any device, and an update of Zigbee2MQTT (new devices, fixes) needs no new firmware here.

## What the node does

- **One client at a time, two addresses at most.** The bridge listens on a TCP port (8888 by default) and accepts only the IPv4 address named in the settings (`zigbee.allowed_client`, the **primary**: the server that runs Zigbee2MQTT; required while the bridge is on) and, optionally, a second one (`zigbee.fallback_client`, the **fallback**: for example an alarm panel). A connection from any other address is closed at once and counted. A second connection from the client that holds the radio replaces the first (a client that vanished without closing would otherwise hold the radio until the keep-alive noticed). TCP keep-alive and a send timeout stop a dead client from holding the bridge.
- **The fallback only gets the radio while the primary is away.** It is accepted only when the primary is not connected and has been away for `zigbee.fallback_after_s` seconds (30 by default; counted from the moment it last left, or from the start of the bridge). The primary may always connect, and when it does it takes the radio back from the fallback. The rules are in `core/zigbee_bridge.hpp` (`Arbiter`), tested on a computer; the status shows who holds the radio (`role`) and how many times the fallback took it over (`takeovers`). The node does not interpret anything the fallback does: what it sends and what it understands is the fallback's business.
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
| `zigbee.allowed_client` | empty | The primary: the one address that may always connect (the server). Required. |
| `zigbee.fallback_client` | empty | Optional second address (an alarm panel) that may take the radio over while the primary is away. Not the same as the primary. |
| `zigbee.fallback_after_s` | 30 | Seconds the primary must have been away (5 to 600). |

The pins may not be reserved by the board, shared with the meters' line, a relay or an analog input, or each other. The settings apply after a restart of the node.

## Zigbee2MQTT

In `configuration.yaml` of Zigbee2MQTT, on the machine whose address is `allowed_client`:

```yaml
serial:
  port: tcp://<node address>:8888
  adapter: zstack
```

The radio must carry a **Z-Stack coordinator firmware built for the CC2652P** (the P version has an internal power amplifier; a build for the CC2652R or RB is the wrong one). The firmware the module is sold with is the maker's own self-networking program (HEX commands, 230400 baud only): it is **not** the one Zigbee2MQTT expects.

## Radio modules that work with this bridge

The node does not care which chip the radio is: it lends a serial line byte for byte, and what matters is that the module runs a coordinator firmware that Zigbee2MQTT knows. So the choice of module is a hardware choice, and **older and newer chips are all supported**; the first board is built with the newest one.

| Module | Chip | Flash / RAM | Notes |
|---|---|---|---|
| **RF-star RF-BM-2652P4** (preferred for the first boards; **RF-BM-2652P4I** has an IPEX connector) | TI **CC2652P7** | 704 KB / 152 KB (144 KB SRAM + 8 KB cache) | Twice the memory of the CC2652P. Zigbee 3.0, **Thread, Matter** and Bluetooth 5.3 LE on the same module (concurrent with the Dynamic Multiprotocol Manager), 20 dBm power amplifier, -99 dBm receiver, 1.8 - 3.8 V, 23 GPIO, 29 half-holes with 1.27 mm pitch, 30.0 x 16.4 x 2.2 mm. Sold bare: it has no serial firmware until one is flashed. |
| Modules with a TI **CC2674P10** (the same SimpleLink family, one step above the CC2652P7) | TI CC2674P10 | 1024 KB / 256 KB | Cortex-M33 with 1 MB of flash and 256 KB of RAM, 20 dBm power amplifier: the most room for Matter and Thread plus a double-bank over-the-air update. Supported in the same way: it needs a Z-Stack coordinator image built for it, and the manufacturer's datasheet for the chosen module fixes its pins and antenna notes (to be added when the module is chosen). |
| **SMLIGHT SLRM-P10** (the newest option; a CC2674P10 module) | TI **CC2674P10** | up to 1024 KB / 256 KB | Cortex-M33 at 48 MHz, Zigbee 3.0, Thread (so Matter over Thread) and Bluetooth 5.3 LE, 20 dBm power amplifier, 1.8 - 3.8 V, up to 31 GPIO with 2 UART, 2 SPI and 2 I2C, a ROM boot loader and an SWD/JTAG debug port, stamp-hole package with 1.27 mm pitch, 16.4 x 27.0 x 2.7 mm. Its web page does not publish a pinout, an antenna option, what firmware it ships with or a datasheet, and gives the RAM as 256 KB in one place and 296 KB in another: get the datasheet from the maker before routing a board. Paired with an ESP32-S3 Ethernet board with PoE it is the combination with no limits on memory or protocols. |
| Ebyte E72-2G4M20S1E | TI CC2652P | 352 KB / 80 KB | The module the first proposal used; still supported as it was. |
| Any other CC2652P / CC2652P7 / CC2652R board | TI CC26x2 | - | Works if it carries a Z-Stack **coordinator** image built for it. |
| Silicon Labs and other families | EFR32 and others | - | Not tried here. A coordinator that Zigbee2MQTT drives over a plain serial line at the speed this bridge keeps (115200 baud, no flow control) can sit behind the bridge; one that needs another speed or hardware flow control cannot without a change to the bridge's UART settings. |

### The RF-BM-2652P4 on the base board

- **Supply:** 3.3 V; it draws 101 mA at +20 dBm in transmit, so keep the 30 % margin of the maker's advice on the 3.3 V rail and a decoupling capacitor close to the VCC pin (pin 13).
- **Reset and bootloader:** RESET_N is pin 10 (internal pull-up, active low), which is the line the node's `reset` pin drives. The JTAG lines are on pins 11 (TMSC), 12 (TCKC), 17 (DIO16, TDO) and 18 (DIO17, TDI): bring them to a header, because the first image can go in over (c)JTAG when the serial boot loader is not enabled yet. Which DIO is the UART and which the boot loader backdoor is decided by the firmware image: take it from the image's own notes for this chip before routing the board.
- **Power amplifier:** the module switches its PA by itself (DIO28 and DIO29 are used inside the module); do not use them.
- **Antenna:** the P4 has an inverted-F antenna on the board and the P4I an IPEX connector (the same module can be changed between the two by moving a capacitor and a resistor, as the datasheet explains). Inside a metal box use the IPEX version with an antenna outside the box; keep copper and signal lines away from the antenna area, and keep the module away from USB 3.0 and switching supplies.
- **Footprint:** the module is a 29 half-hole stamp, 30.0 x 16.4 mm. A board that must take either module needs both footprints, with one populated.
- **Firmware:** Koenkk's *Z-Stack-firmware* repository carries coordinator images for the CC2652P7 LaunchPad family. As with the E72, the RF-switch and the pins of a third-party module may not be those of the LaunchPad image, so check an image against this module before trusting it. Nothing has been flashed with this project's boards yet.
- **Thread and Matter:** the same module can run other firmware (a Thread radio co-processor for an OpenThread border router, or a multiprotocol image). Because the bridge only carries bytes, a Thread border router on the server would use the same serial line; that is an addition for later, not something this version does.

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

## What is known about the firmware of this module (from its public projects)

- The Ebyte E72-2G4M20S1E is a CC2652P module with its own power amplifier and RF switch. Koenkk's *Z-Stack-firmware* repository carries Z-Stack 3.x **coordinator** and **router** images for the CC2652P family; the E72-specific project *egony/cc2652p_E72-2G4M20S1E* reports that this module's RF generator and RF-switch settings are not the ones of the LaunchPad images and ships its own coordinator image (its `firmware/coordinator` folder), so which image to flash has to be checked against both before the first one.
- The first image goes in over **JTAG** (as a CC1352P1F in the programmer's list); after that the serial bootloader (BSL) can be used, and some modules arrive with it enabled already, which cannot be told in advance. The bridge's reset and bootloader lines (see above) are for that second case.
- Zigbee2MQTT talks to it at **115200 baud without hardware flow control** (`rtscts: false`), the speed this bridge keeps its UART at.
- A router image exists in the same repository (`router/Z-Stack_3.x.0`), which is what the other PoE nodes of the house would run to extend the mesh.
- None of this has been tried with this project's boards: nothing has been flashed, and the choice between the images is still open.
