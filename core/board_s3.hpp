// ARMOR-ELECTRICAL - the two boards this firmware is built for, and which GPIO pins of each may be handed to a serial port.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One firmware, two board profiles (chosen when the image is built, tools/build_node.sh NODE BOARD; the host tests build both):
//
//   s3-wifi   an ESP32-S3-DevKitC-1 style ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM), two USB-C sockets, no Ethernet: Wi-Fi is the only way in.
//   s3-eth    the Waveshare ESP32-S3-ETH: ESP32-S3R8 (8 MB octal PSRAM), 16 MB flash, a W5500 Ethernet controller on SPI (RJ45, and PoE through a separate
//             module), a microSD socket and a camera connector. Ethernet is the way in; Wi-Fi may be kept as an access point.
//
// What both share, from the printed pin maps and the chip's datasheet:
//   - GPIO 22..25 do not exist, and 26..32 are the external flash;
//   - GPIO 33..37 belong to the octal PSRAM (35, 36 and 37 are on the header of the N16R8 module: never use them);
//   - GPIO 19 and 20 are the native USB pair (the log and the flashing);
//   - GPIO 0 (BOOT), 3, 45 and 46 are strapping pins read at reset: usable, but whatever is wired there must not pull them the wrong way while the chip starts;
//   - GPIO 39..42 are the JTAG pins of the chip; they are free unless a JTAG probe is used.
// Only on s3-wifi: GPIO 43 and 44 are UART0's pins to the CH343P of the "USB to serial" socket (usable, a cable in that socket drives them) and GPIO 48
// drives the on-board RGB LED (usable, the LED flickers).
// Only on s3-eth: GPIO 9..14 are the W5500 (reset, interrupt, MOSI, MISO, clock, chip select) and 8 is wired to the camera connector: never offered;
// GPIO 4..7 are the microSD socket: usable when no card is used.
// Nothing here has been checked on a board.
#pragma once
#include <array>
#include <cstdint>

namespace armor::board {

#if defined(ARMOR_BOARD_S3_ETH)
constexpr bool kHasEthernet = true;
constexpr const char* kId = "s3-eth";
constexpr const char* kName = "Waveshare ESP32-S3-ETH";
#else
constexpr bool kHasEthernet = false;
constexpr const char* kId = "s3-wifi";
constexpr const char* kName = "ESP32-S3-WROOM-1 N16R8";
#endif

enum class PinUse : std::uint8_t {
  kFree,      // no special role
  kCaution,   // a strapping pin, the boot button, the USB-serial pair, the LED or the SD socket: usable with care
  kReserved,  // taken by the board or by the chip: never offered
};

enum class Reserved : std::uint8_t { kNone, kNoSuchPin, kFlash, kPsram, kUsb, kEthernet, kCamera };

struct PinInfo {
  int gpio;
  PinUse use;
  Reserved reason;   // only meaningful for kReserved
  const char* note;  // "" or a short code the panel translates: "strapping", "boot", "usb_serial", "led", "sdcard"
  bool on_header;
};

constexpr int kFirstGpio = 0;
constexpr int kLastGpio = 48;

constexpr PinInfo pin_info(int gpio) {
  if (gpio < kFirstGpio || gpio > kLastGpio || (gpio >= 22 && gpio <= 25)) return {gpio, PinUse::kReserved, Reserved::kNoSuchPin, "", false};
  if (gpio >= 26 && gpio <= 32) return {gpio, PinUse::kReserved, Reserved::kFlash, "", false};
  if (gpio >= 33 && gpio <= 37) return {gpio, PinUse::kReserved, Reserved::kPsram, "", gpio >= 35 && !kHasEthernet};
  if (gpio == 19 || gpio == 20) return {gpio, PinUse::kReserved, Reserved::kUsb, "", true};
  if (gpio == 0) return {gpio, PinUse::kCaution, Reserved::kNone, "boot", true};
  if (gpio == 3 || gpio == 45 || gpio == 46) return {gpio, PinUse::kCaution, Reserved::kNone, "strapping", true};
#if defined(ARMOR_BOARD_S3_ETH)
  if (gpio >= 9 && gpio <= 14) return {gpio, PinUse::kReserved, Reserved::kEthernet, "", false};
  if (gpio == 8) return {gpio, PinUse::kReserved, Reserved::kCamera, "", false};
  if (gpio >= 4 && gpio <= 7) return {gpio, PinUse::kCaution, Reserved::kNone, "sdcard", true};
#else
  if (gpio == 43 || gpio == 44) return {gpio, PinUse::kCaution, Reserved::kNone, "usb_serial", true};
  if (gpio == 48) return {gpio, PinUse::kCaution, Reserved::kNone, "led", true};
#endif
  return {gpio, PinUse::kFree, Reserved::kNone, "", true};
}

constexpr bool assignable(int gpio) { return pin_info(gpio).use != PinUse::kReserved; }

// The serial ports of a node: three hardware UARTs (any pins, through the chip's GPIO matrix) and seven emulated ones. The console is on the native USB, so
// UART0 is free for a device.
constexpr int kPortCount = 10;
constexpr int kHardwarePorts = 3;
constexpr int kSoftPorts = 7;

// Where the ports are wired by default: (rx, tx, de). None is a reserved pin. -1: no direction pin.
struct DefaultPins { int rx, tx, de; };
#if defined(ARMOR_BOARD_S3_ETH)
// The W5500 takes GPIO 9..14 and the camera connector 8, so the ports move: 4 and 5 are the microSD socket (no card, no problem). The tenth port has no
// free pin pair left and sits on strapping pins (3 and 46): use it last, and only with equipment whose idle level does not disturb the boot.
constexpr std::array<DefaultPins, kPortCount> kDefaultPins{{
    {16, 15, 7},    // port 1: UART1
    {18, 17, 6},    // port 2: UART2
    {4, 5, -1},     // port 3: UART0 (the microSD socket's pins)
    {1, 2, -1},     // port 4: emulated
    {21, 47, -1},   // port 5: emulated
    {38, 39, -1},   // port 6: emulated
    {40, 41, -1},   // port 7: emulated
    {42, 48, -1},   // port 8: emulated
    {43, 44, -1},   // port 9: emulated
    {3, 46, -1},    // port 10: emulated, on strapping pins
}};
#else
constexpr std::array<DefaultPins, kPortCount> kDefaultPins{{
    {16, 15, 7},    // port 1: UART1
    {18, 17, 8},    // port 2: UART2
    {4, 5, 6},      // port 3: UART0 (not on the pins of the USB-serial socket)
    {9, 10, -1},    // port 4: emulated
    {11, 12, -1},   // port 5: emulated
    {13, 14, -1},   // port 6: emulated
    {21, 47, -1},   // port 7: emulated
    {38, 39, -1},   // port 8: emulated
    {40, 41, -1},   // port 9: emulated
    {42, 1, -1},    // port 10: emulated
}};
#endif

}  // namespace armor::board
