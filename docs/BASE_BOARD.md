# The base board: what carries the ESP32-S3 module

The electrical node is an **ESP32-S3** module (the Waveshare ESP32-S3-ETH, the `s3-eth` profile) that plugs into a **base board**. The base board adds what the module does not have:

- a **Zigbee radio** on its own serial line ([ZIGBEE.md](ZIGBEE.md)),
- up to **eight relay outputs**,
- up to **eight analog inputs** (a voltage, or a current from a clamp or a Hall sensor),

next to the serial line of the PZEM meters the node already has. Every pin is a setting in the panel; this page is the proposal the firmware's defaults follow, and the reasons. **Nothing here has been built or connected**; the pins are taken from the board's printed pin map and the chip's datasheet and must be checked against the real module before a board is made.

## Pins (profile `s3-eth`)

| Use | GPIO | Notes |
|---|---|---|
| Meters' line (UART1) | RX 16, TX 15 | PZEM 5 V side needs a level adaptation, see [HARDWARE.md](HARDWARE.md). |
| Zigbee radio (UART2) | RX 18, TX 17 | RX receives the radio's TX (module pin 8, DIO13); TX sends to its RX (pin 7, DIO12). |
| Radio RESET_N | 43 | Module pin 24, active low, open-drain from the node. |
| Radio boot loader | 44 | Module pin 10 (DIO15), active low, open-drain from the node. |
| Relays 1 to 8 | 38, 39, 40, 41, 42, 47, 48, 21 | Outputs. |
| Analog inputs 1 to 6 | 1, 2, 4, 5, 6, 7 | ADC1. Inputs 7 and 8 have no default pin. |
| Not available | 8 to 14 | The W5500 Ethernet controller (9 to 14) and the camera connector (8). |
| Left alone | 0, 3, 45, 46 | Strapping pins read at reset (0 is the BOOT button). GPIO 3 may be given to an analog input if needed. |
| Left alone | 19, 20 | The native USB (the log and the flashing). |

Why these:

- **No relay on GPIO 43 or 44.** They are UART0's pins, where the chip's ROM prints its start-up messages for a moment at every reset. A relay there would twitch at every start. The radio's two control lines go there instead: they are open-drain, and a radio that is reset while the node starts does no harm.
- **Analog inputs only on ADC1 (GPIO 1 to 10).** ADC2 is shared with the Wi-Fi radio and may refuse a reading while it is in use, so the panel never offers it. On the Ethernet module GPIO 9 and 10 belong to the W5500 and 4 to 7 to the microSD socket (usable only with **no card** in it).
- **Strapping pins untouched** by default: whatever is wired to them must not pull them the wrong way while the chip starts.

The same firmware also builds for the plain Wi-Fi board (`s3-wifi`), where the defaults differ (the radio's control lines on 21 and 47, relays on 38 to 42 and 11 to 13, and analog inputs on 1 to 9 except 3); the panel's pin lists show what each board allows.

## The Zigbee module (Ebyte E72-2G4M20S1E)

Pins from the maker's manual (stamp-hole package, 1.27 mm pitch, 17.5 x 28.7 mm):

| Module pin | Name | Use |
|---|---|---|
| 20 | VCC | 1.9 to 3.8 V; **more than 3.8 V destroys it**. Use the 3.3 V rail. |
| 1, 11, 12, 19, 23, 34 | GND | |
| 7 | UART_RX (DIO12) | From the node's TX. |
| 8 | UART_TX (DIO13) | To the node's RX. |
| 24 | RESET_N | Active low. |
| 10 | BOOT_LOADER (DIO15) | Active low; low at reset enters the serial boot loader. |
| 2 | STATUS_LED (DIO7) | Optional LED: long on = standby, fast blink = joining, 1 s blink = allowing joins (of the maker's firmware). |
| 13, 14 | JTAG_TMSC, JTAG_TCKC | Bring them, with 15 and 16 (TDO, TDI), to a small header: the way back if the boot loader is ever lost. |

Design notes: put **10 uF and 100 nF** at VCC; a **10 kohm pull-up to 3.3 V on RESET_N and on the boot line** (the node only pulls them low); keep copper, ground and metal **away from the antenna end** of the module and keep the module's edge at the board's edge; keep high-frequency traces and switching supplies away from it. The maker gives a transmit current of 106 mA: size the 3.3 V rail with plenty of margin.

## Relay outputs

The node drives a GPIO; the driver, the relay and the load are the installer's.

- **Every relay starts open** (off), whatever it was before: a node that restarts never closes anything by itself. For that to hold while the chip starts (when its pins float), the board must **pull every driver input to its open level** with its own resistor: 10 kohm to ground for an active-high driver (a transistor or a ULN2803), 10 kohm to 3.3 V for an active-low one (an opto-isolated relay board). The panel's *Closes when the pin is low* setting says which kind each relay is.
- Use **drivers with flyback diodes** (a ULN2803, or a transistor and a diode per coil) or relay modules with optical isolation, supplied from their own rail (5 V or 12 V) with the grounds joined deliberately.
- **Mains never reaches the node's board.** A relay's contacts are wired by someone qualified, inside the panel, with the protection the load needs; a load that must not lose its supply (a freezer) has no business behind one of these without the installation's own safeguards. These are plain relays closed by an administrator from the panel; they are not the source-transfer controller of [SWITCHING.md](SWITCHING.md), which is not linked into the firmware.
- **From the server (optional).** With `remote.relays` on, each relay is also a device of the house (`armor/device/<node>/<relay>/state` and `/set`, see `core/relay_remote.hpp`) and falls back to open when the broker is lost for `remote.link_timeout_s`. It is off by default and needs the broker's access list to allow it (`mqtt_identity.sh electrical-relays <node> on`) and a device created in Studio with the connection *ARMOR node* and the name `<node>/<relay>`.

## Analog inputs

A 12-bit converter with the chip's calibration; the range read is **0 to about 3.1 V** (12 dB attenuation). **Never put more than 3.3 V on a pin**: add clamp diodes (for example a BAT54S to 3.3 V and ground) and a series resistor (1 kohm), and a **100 nF** capacitor at the pin; keep the source impedance under about 10 kohm. In the panel each input has:

- **what it measures** (a voltage or a current), **average (DC)** or **effective value (AC)**;
- the **scale**: units per volt at the pin (a 1:11 divider is 11; a clamp that gives 1 V for 30 A is 30);
- for the average, the **volts at zero** (a Hall sensor biased at 2.5 V, scaled to the pin), and for any, a value under which it **shows zero** (the noise floor);
- for a current, an optional **nominal voltage** to give a power and an energy (the current times the voltage the circuit is assumed to have; the energy counts since the node started).

Typical front ends: a **resistor divider** for a battery or a PV string (with the capacitor above); a **current clamp with voltage output** (for example 1 V for 30 A or 100 A) biased to mid-supply through two equal resistors, in AC mode; a **voltage-transformer module** for the mains voltage, in AC mode (isolated, wired by a qualified person); a **Hall sensor** scaled to 3.3 V, in DC mode with its zero-volts offset. The result is a channel of the node's message (`armor/electrical/<node>/state`), after the meters' (sixteen channels in all), with only what the input measures.

## Before the first power-up

1. Check every pin against the real module's documentation and the board's schematic; the pin map above is from the printed pin map.
2. Power the base board without the module, check the rails, then add the module.
3. Connect nothing to mains. Try the relays with a lamp behind an isolating transformer, as in [SAFETY.md](SAFETY.md).
4. Read the log (*Firmware and log*) for the lines of the bridge, the relays and the inputs.
