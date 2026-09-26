# The protocols: what is decoded, from where, and what is not

## PZEM-004T v3 and PZEM-017 (Peacefair)

From the makers' public description of the two meters; **nothing here has been connected to a meter.**

- Serial 9600 baud 8N1. Modbus RTU, function `0x04` (read input registers). Request: address, `0x04`, first register, count, CRC-16/MODBUS (low byte first): the request for the ten registers of the address 1 is `01 04 00 00 00 0A 70 0D`. Reply: address, `0x04`, byte count, the registers (high byte first), CRC. A refusal answers `0x84` and an exception code.
- **PZEM-004T v3 (AC, up to 260 V, a current transformer):** ten registers from `0x0000`: voltage (0.1 V), current (0.001 A, two registers, low word first), power (0.1 W, two registers), energy (1 Wh, two registers), frequency (0.1 Hz), power factor (0.01), alarm (`0xFFFF` above the threshold).
- **PZEM-017 (DC, up to 300 V, a shunt):** eight registers: voltage (0.01 V), current (0.01 A), power (0.1 W, two registers), energy (1 Wh, two registers), the high- and the low-voltage alarm (`0xFFFF` when active).
- Several meters share a line with **one address each**. A meter's address is set once, with the maker's tool, one meter at a time, before they are installed; the request that changes it is not built here.
- The address `0xF8` answers whichever meter is on the line: for a bench with one meter only.
- A reply is accepted only when its CRC, address, function and length fit, and its values are within what a real meter can give.

## Honest limits

- The register layouts come from the makers' descriptions and were not checked against a meter. The first reading of a real one must be compared with the meter's own display.
- The current transformer of the PZEM-004T v3 measures the current, not its direction: the power is always positive. To tell import from export on the grid input a meter that measures direction is needed (a later step).
- The line's electrical levels (the PZEM's serial side is 5 V, an ESP32-S3's is 3.3 V) need a level adaptation that this project does not describe yet.
