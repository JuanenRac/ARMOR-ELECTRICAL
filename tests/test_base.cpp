// ARMOR-ELECTRICAL - host tests of what every node shares and that has no hardware: the schedule of retries, Improv Wi-Fi over serial and the stack report.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing was run on a board; the packets are the ones the standard (https://www.improv-wifi.com/serial/) describes.
#include <cstdio>
#include <string>
#include <vector>

// a node keeps its core next to its sources (the radar node: main/core), the others in core/
#if __has_include("../core/auth.hpp")
#include "../core/admin_sources.hpp"
#include "../core/auth.hpp"
#include "../core/backoff.hpp"
#include "../core/ble_dispatch.hpp"
#include "../core/ble_frame.hpp"
#include "../core/ed25519_verify.hpp"
#include "../core/fw_trust.hpp"
#include "../core/improv.hpp"
#include "../core/json.hpp"
#include "../core/node_health.hpp"
#include "../core/task_health.hpp"
#else
#include "../main/core/admin_sources.hpp"
#include "../main/core/auth.hpp"
#include "../main/core/backoff.hpp"
#include "../main/core/ble_dispatch.hpp"
#include "../main/core/ble_frame.hpp"
#include "../main/core/ed25519_verify.hpp"
#include "../main/core/fw_trust.hpp"
#include "../main/core/improv.hpp"
#include "../main/core/json.hpp"
#include "../main/core/node_health.hpp"
#include "../main/core/task_health.hpp"
#endif
// the broker's address is a setting of the nodes that have a broker of their own to name
#if __has_include("../core/broker_uri.hpp")
#define ARMOR_HAS_BROKER_URI 1
#include "../core/broker_uri.hpp"
#endif

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// ---- the schedule of retries -----------------------------------------------------------------------------------------------------------

struct Ed25519Vector {
  std::uint8_t public_key[32];
  std::uint8_t signature[64];
  std::uint8_t message[300];
  std::size_t length;
};
// made by Node's own Ed25519 (a different implementation) from fixed seeds
static const Ed25519Vector kEd25519Vectors[] = {
  {{0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0, 0x2a, 0x6f, 0x0d, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, 0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda, 0x29}, {0x8f, 0x89, 0x5b, 0x3c, 0xaf, 0xe2, 0xc9, 0x50, 0x60, 0x39, 0xd0, 0xe2, 0xa6, 0x63, 0x82, 0x56, 0x80, 0x04, 0x67, 0x4f, 0xe8, 0xd2, 0x37, 0x78, 0x50, 0x92, 0xe4, 0x0d, 0x6a, 0xaf, 0x48, 0x3e, 0x4f, 0xc6, 0x01, 0x68, 0x70, 0x5f, 0x31, 0xf1, 0x01, 0x59, 0x61, 0x38, 0xce, 0x21, 0xaa, 0x35, 0x7c, 0x0d, 0x32, 0xa0, 0x64, 0xf4, 0x23, 0xdc, 0x3e, 0xe4, 0xaa, 0x3a, 0xbf, 0x53, 0xf8, 0x03}, {}, 0},
  {{0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0, 0x2a, 0x6f, 0x0d, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, 0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda, 0x29}, {0x88, 0x5d, 0xfb, 0x07, 0xca, 0xb2, 0x79, 0x6e, 0xb9, 0x60, 0x53, 0x1a, 0x2f, 0x09, 0xb9, 0x72, 0xad, 0x59, 0xb9, 0x7b, 0xb1, 0x25, 0xbe, 0xf5, 0xfd, 0xda, 0x08, 0x55, 0xd6, 0xbe, 0xbe, 0xbf, 0x24, 0x44, 0x7e, 0x70, 0x5f, 0xa1, 0x15, 0x75, 0x63, 0x9d, 0xf3, 0x96, 0xc2, 0x01, 0xcc, 0xf5, 0x2a, 0x1a, 0x16, 0xb0, 0x14, 0xa7, 0xa2, 0xf0, 0xce, 0x73, 0xa7, 0xa1, 0x61, 0x75, 0x73, 0x08}, {0x61, 0x62, 0x63}, 3},
  {{0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0, 0x2a, 0x6f, 0x0d, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, 0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda, 0x29}, {0xbb, 0x83, 0x26, 0x95, 0x37, 0x55, 0x0f, 0x47, 0x33, 0xa0, 0x31, 0x5b, 0xbb, 0xcf, 0x7e, 0xbd, 0x9e, 0x01, 0x7c, 0x07, 0x90, 0x35, 0xc6, 0xe6, 0xaf, 0xe5, 0x62, 0x41, 0xe4, 0xb5, 0x9c, 0x4a, 0xe0, 0x19, 0xa0, 0xd1, 0xca, 0x6a, 0x89, 0xff, 0xd5, 0xab, 0x25, 0x2b, 0x0d, 0x05, 0x48, 0x4f, 0x19, 0x91, 0x10, 0xc3, 0x9d, 0x39, 0x65, 0xdf, 0x4e, 0x33, 0xa1, 0x1b, 0x54, 0xf6, 0x57, 0x06}, {0x61, 0x72, 0x6d, 0x6f, 0x72, 0x2d, 0x72, 0x61, 0x64, 0x61, 0x72, 0x2d, 0x30, 0x2e, 0x36, 0x2e, 0x36, 0x2d, 0x73, 0x33, 0x2d, 0x65, 0x74, 0x68}, 24},
  {{0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0, 0x2a, 0x6f, 0x0d, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, 0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda, 0x29}, {0xcf, 0x09, 0xf8, 0x34, 0x4c, 0x8d, 0x15, 0x3d, 0x4f, 0xe3, 0x6f, 0xc6, 0x34, 0x2e, 0x27, 0xa2, 0x2b, 0xdb, 0xc5, 0x48, 0x66, 0xf5, 0x98, 0x9a, 0x2a, 0xa0, 0xa7, 0x1c, 0xbc, 0xae, 0x94, 0x67, 0x86, 0xf2, 0x8f, 0x9c, 0x01, 0xa6, 0x13, 0xb4, 0xe9, 0xf6, 0x1d, 0x71, 0x5f, 0xb7, 0xad, 0xf8, 0xdc, 0x01, 0xb0, 0x4a, 0xc6, 0x6c, 0x89, 0x5a, 0xc3, 0xa4, 0x1a, 0xd0, 0x35, 0x77, 0xb1, 0x0f}, {0x00, 0x0d, 0x1a, 0x27, 0x34, 0x41, 0x4e, 0x5b, 0x68, 0x75, 0x82, 0x8f, 0x9c, 0xa9, 0xb6, 0xc3, 0xd0, 0xdd, 0xea, 0xf7, 0x04, 0x11, 0x1e, 0x2b, 0x38, 0x45, 0x52, 0x5f, 0x6c, 0x79, 0x86, 0x93, 0xa0, 0xad, 0xba, 0xc7, 0xd4, 0xe1, 0xee, 0xfb, 0x08, 0x15, 0x22, 0x2f, 0x3c, 0x49, 0x56, 0x63, 0x70, 0x7d, 0x8a, 0x97, 0xa4, 0xb1, 0xbe, 0xcb, 0xd8, 0xe5, 0xf2, 0xff, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4d, 0x5a, 0x67, 0x74, 0x81, 0x8e, 0x9b, 0xa8, 0xb5, 0xc2, 0xcf, 0xdc, 0xe9, 0xf6, 0x03, 0x10, 0x1d, 0x2a, 0x37, 0x44, 0x51, 0x5e, 0x6b, 0x78, 0x85, 0x92, 0x9f, 0xac, 0xb9, 0xc6, 0xd3, 0xe0, 0xed, 0xfa, 0x07, 0x14, 0x21, 0x2e, 0x3b, 0x48, 0x55, 0x62, 0x6f, 0x7c, 0x89, 0x96, 0xa3, 0xb0, 0xbd, 0xca, 0xd7, 0xe4, 0xf1, 0xfe, 0x0b, 0x18, 0x25, 0x32, 0x3f, 0x4c, 0x59, 0x66, 0x73, 0x80, 0x8d, 0x9a, 0xa7, 0xb4, 0xc1, 0xce, 0xdb, 0xe8, 0xf5, 0x02, 0x0f, 0x1c, 0x29, 0x36, 0x43, 0x50, 0x5d, 0x6a, 0x77, 0x84, 0x91, 0x9e, 0xab, 0xb8, 0xc5, 0xd2, 0xdf, 0xec, 0xf9, 0x06, 0x13, 0x20, 0x2d, 0x3a, 0x47, 0x54, 0x61, 0x6e, 0x7b, 0x88, 0x95, 0xa2, 0xaf, 0xbc, 0xc9, 0xd6, 0xe3, 0xf0, 0xfd, 0x0a, 0x17, 0x24, 0x31, 0x3e, 0x4b, 0x58, 0x65, 0x72, 0x7f, 0x8c, 0x99, 0xa6, 0xb3, 0xc0, 0xcd, 0xda, 0xe7, 0xf4, 0x01, 0x0e, 0x1b, 0x28, 0x35, 0x42, 0x4f, 0x5c, 0x69, 0x76, 0x83, 0x90, 0x9d, 0xaa, 0xb7, 0xc4, 0xd1, 0xde, 0xeb, 0xf8, 0x05, 0x12, 0x1f, 0x2c, 0x39, 0x46, 0x53, 0x60, 0x6d, 0x7a, 0x87, 0x94, 0xa1, 0xae, 0xbb, 0xc8, 0xd5, 0xe2, 0xef, 0xfc, 0x09, 0x16, 0x23, 0x30, 0x3d, 0x4a, 0x57, 0x64, 0x71, 0x7e, 0x8b, 0x98, 0xa5, 0xb2, 0xbf, 0xcc, 0xd9, 0xe6, 0xf3, 0x00, 0x0d, 0x1a, 0x27, 0x34, 0x41, 0x4e, 0x5b, 0x68, 0x75, 0x82, 0x8f, 0x9c, 0xa9, 0xb6, 0xc3, 0xd0, 0xdd, 0xea, 0xf7, 0x04, 0x11, 0x1e, 0x2b, 0x38, 0x45, 0x52, 0x5f, 0x6c, 0x79, 0x86, 0x93, 0xa0, 0xad, 0xba, 0xc7, 0xd4, 0xe1, 0xee, 0xfb, 0x08, 0x15, 0x22, 0x2f}, 300},
  {{0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0, 0x2a, 0x6f, 0x0d, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, 0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda, 0x29}, {0x88, 0xe8, 0xb0, 0x93, 0xed, 0x8f, 0x86, 0x57, 0x48, 0x8c, 0x35, 0x9c, 0x89, 0xa3, 0x60, 0xce, 0x89, 0x83, 0x29, 0xab, 0x02, 0xb0, 0xee, 0x18, 0x69, 0x15, 0x30, 0xa6, 0xfc, 0x1d, 0x7c, 0xe3, 0xaa, 0x4e, 0x5c, 0xc4, 0x24, 0x14, 0x3f, 0xe7, 0x94, 0xd7, 0x92, 0xea, 0xd6, 0x58, 0xa4, 0xa4, 0xdc, 0xe4, 0x9a, 0x61, 0x89, 0x2c, 0xf1, 0x79, 0x67, 0x03, 0x26, 0xeb, 0x8e, 0x82, 0xf8, 0x0a}, {0xa5, 0xdf, 0x3f, 0xde, 0x7b, 0x20, 0x0c, 0x3f, 0xa2, 0x79, 0x11, 0x44, 0xc7, 0xe0, 0x5c, 0x5c, 0x2b, 0xce, 0xd4, 0xc6, 0xad, 0x86, 0x3d, 0x3a, 0x4c, 0xf7, 0x6c, 0x97, 0x2c, 0x17, 0x12, 0xf9}, 32},
  {{0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}, {0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b, 0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b}, {}, 0},
  {{0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}, {0x80, 0xd7, 0x24, 0xb0, 0x1e, 0x7c, 0xa2, 0x60, 0xf4, 0xcc, 0x7f, 0x8d, 0xe7, 0xc9, 0x5f, 0x73, 0xcf, 0xac, 0x61, 0x5b, 0xab, 0x1f, 0x76, 0x2b, 0x64, 0x35, 0xb6, 0xec, 0x26, 0xc8, 0xcf, 0x6d, 0x2c, 0x75, 0x8d, 0xae, 0x2f, 0x87, 0x39, 0x9a, 0x8e, 0xed, 0xa1, 0xcb, 0xcd, 0x28, 0x35, 0xac, 0x5b, 0xa6, 0x6d, 0x6e, 0xca, 0xa3, 0xab, 0xa5, 0xe5, 0x67, 0xa7, 0x51, 0x05, 0x3d, 0xc2, 0x07}, {0x61, 0x62, 0x63}, 3},
  {{0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}, {0xd2, 0xc3, 0xc7, 0xe7, 0x7d, 0x1f, 0x74, 0x9f, 0xa1, 0x03, 0x4d, 0x28, 0x96, 0xd3, 0x82, 0x5b, 0xb5, 0xf6, 0x7f, 0xd7, 0xe3, 0x9f, 0x1f, 0xb9, 0x80, 0xcd, 0x71, 0x82, 0x49, 0xc7, 0xc7, 0xa5, 0xaa, 0x83, 0xa7, 0xb0, 0xc9, 0x90, 0x7a, 0x28, 0x07, 0xfa, 0xd5, 0xc3, 0x87, 0x24, 0x3c, 0xa4, 0x29, 0xa8, 0x62, 0xe5, 0x6f, 0xd5, 0x0f, 0xad, 0xb0, 0xc9, 0x7e, 0x6f, 0x32, 0xa1, 0x0b, 0x09}, {0x61, 0x72, 0x6d, 0x6f, 0x72, 0x2d, 0x72, 0x61, 0x64, 0x61, 0x72, 0x2d, 0x30, 0x2e, 0x36, 0x2e, 0x36, 0x2d, 0x73, 0x33, 0x2d, 0x65, 0x74, 0x68}, 24},
  {{0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}, {0xc4, 0x59, 0x49, 0x9f, 0xdf, 0x7d, 0xfd, 0x70, 0x39, 0x26, 0x51, 0xea, 0x98, 0xe9, 0x6b, 0x5d, 0xb0, 0x29, 0x00, 0x5b, 0xb9, 0x27, 0x9f, 0xd9, 0x5b, 0x80, 0x8a, 0x08, 0xd7, 0xaf, 0xaf, 0x97, 0xf3, 0x51, 0xca, 0xc5, 0xd6, 0x76, 0x8f, 0xaa, 0xc4, 0x9f, 0x83, 0x96, 0x13, 0x94, 0x85, 0xd5, 0x8f, 0x0f, 0xb7, 0x29, 0x92, 0xee, 0xd2, 0x1f, 0x39, 0x87, 0xae, 0xf1, 0xad, 0x11, 0x01, 0x0e}, {0x00, 0x0d, 0x1a, 0x27, 0x34, 0x41, 0x4e, 0x5b, 0x68, 0x75, 0x82, 0x8f, 0x9c, 0xa9, 0xb6, 0xc3, 0xd0, 0xdd, 0xea, 0xf7, 0x04, 0x11, 0x1e, 0x2b, 0x38, 0x45, 0x52, 0x5f, 0x6c, 0x79, 0x86, 0x93, 0xa0, 0xad, 0xba, 0xc7, 0xd4, 0xe1, 0xee, 0xfb, 0x08, 0x15, 0x22, 0x2f, 0x3c, 0x49, 0x56, 0x63, 0x70, 0x7d, 0x8a, 0x97, 0xa4, 0xb1, 0xbe, 0xcb, 0xd8, 0xe5, 0xf2, 0xff, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4d, 0x5a, 0x67, 0x74, 0x81, 0x8e, 0x9b, 0xa8, 0xb5, 0xc2, 0xcf, 0xdc, 0xe9, 0xf6, 0x03, 0x10, 0x1d, 0x2a, 0x37, 0x44, 0x51, 0x5e, 0x6b, 0x78, 0x85, 0x92, 0x9f, 0xac, 0xb9, 0xc6, 0xd3, 0xe0, 0xed, 0xfa, 0x07, 0x14, 0x21, 0x2e, 0x3b, 0x48, 0x55, 0x62, 0x6f, 0x7c, 0x89, 0x96, 0xa3, 0xb0, 0xbd, 0xca, 0xd7, 0xe4, 0xf1, 0xfe, 0x0b, 0x18, 0x25, 0x32, 0x3f, 0x4c, 0x59, 0x66, 0x73, 0x80, 0x8d, 0x9a, 0xa7, 0xb4, 0xc1, 0xce, 0xdb, 0xe8, 0xf5, 0x02, 0x0f, 0x1c, 0x29, 0x36, 0x43, 0x50, 0x5d, 0x6a, 0x77, 0x84, 0x91, 0x9e, 0xab, 0xb8, 0xc5, 0xd2, 0xdf, 0xec, 0xf9, 0x06, 0x13, 0x20, 0x2d, 0x3a, 0x47, 0x54, 0x61, 0x6e, 0x7b, 0x88, 0x95, 0xa2, 0xaf, 0xbc, 0xc9, 0xd6, 0xe3, 0xf0, 0xfd, 0x0a, 0x17, 0x24, 0x31, 0x3e, 0x4b, 0x58, 0x65, 0x72, 0x7f, 0x8c, 0x99, 0xa6, 0xb3, 0xc0, 0xcd, 0xda, 0xe7, 0xf4, 0x01, 0x0e, 0x1b, 0x28, 0x35, 0x42, 0x4f, 0x5c, 0x69, 0x76, 0x83, 0x90, 0x9d, 0xaa, 0xb7, 0xc4, 0xd1, 0xde, 0xeb, 0xf8, 0x05, 0x12, 0x1f, 0x2c, 0x39, 0x46, 0x53, 0x60, 0x6d, 0x7a, 0x87, 0x94, 0xa1, 0xae, 0xbb, 0xc8, 0xd5, 0xe2, 0xef, 0xfc, 0x09, 0x16, 0x23, 0x30, 0x3d, 0x4a, 0x57, 0x64, 0x71, 0x7e, 0x8b, 0x98, 0xa5, 0xb2, 0xbf, 0xcc, 0xd9, 0xe6, 0xf3, 0x00, 0x0d, 0x1a, 0x27, 0x34, 0x41, 0x4e, 0x5b, 0x68, 0x75, 0x82, 0x8f, 0x9c, 0xa9, 0xb6, 0xc3, 0xd0, 0xdd, 0xea, 0xf7, 0x04, 0x11, 0x1e, 0x2b, 0x38, 0x45, 0x52, 0x5f, 0x6c, 0x79, 0x86, 0x93, 0xa0, 0xad, 0xba, 0xc7, 0xd4, 0xe1, 0xee, 0xfb, 0x08, 0x15, 0x22, 0x2f}, 300},
  {{0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}, {0xf2, 0xbf, 0x25, 0x96, 0x0b, 0x9a, 0x37, 0xa1, 0xbe, 0xdf, 0x04, 0x1d, 0x0d, 0x3f, 0xfa, 0x1e, 0xc2, 0x04, 0x70, 0xa5, 0xba, 0x87, 0x43, 0x4a, 0x42, 0x30, 0x78, 0x48, 0x9d, 0x19, 0x2b, 0x63, 0x7f, 0x16, 0xfb, 0x58, 0xf6, 0x2e, 0x0c, 0xd2, 0x52, 0x7f, 0xde, 0x8c, 0x5e, 0x9e, 0xff, 0xad, 0x86, 0x02, 0x6c, 0x2c, 0x77, 0x4a, 0xbb, 0xba, 0xce, 0x63, 0x76, 0x48, 0xdf, 0x3a, 0x6e, 0x04}, {0xa5, 0xdf, 0x3f, 0xde, 0x7b, 0x20, 0x0c, 0x3f, 0xa2, 0x79, 0x11, 0x44, 0xc7, 0xe0, 0x5c, 0x5c, 0x2b, 0xce, 0xd4, 0xc6, 0xad, 0x86, 0x3d, 0x3a, 0x4c, 0xf7, 0x6c, 0x97, 0x2c, 0x17, 0x12, 0xf9}, 32},
  {{0x75, 0x5c, 0x4c, 0xb9, 0x25, 0x6c, 0xa7, 0xcd, 0xc4, 0xac, 0xfd, 0xc6, 0xcf, 0xee, 0xda, 0x84, 0x90, 0x17, 0xe5, 0xb9, 0xf9, 0x51, 0x4e, 0x99, 0x19, 0x1b, 0xd6, 0x7e, 0x0b, 0x0d, 0x42, 0x76}, {0x92, 0x8b, 0xab, 0x90, 0xda, 0xc5, 0xd9, 0x97, 0x4f, 0x2f, 0x02, 0x7f, 0x6e, 0x6a, 0x18, 0x18, 0x59, 0x8d, 0xaf, 0xc0, 0x0a, 0x60, 0xf1, 0x56, 0x16, 0x10, 0xbd, 0x98, 0x02, 0x48, 0x25, 0x2d, 0x5f, 0x59, 0xa6, 0xb1, 0x9d, 0x81, 0x7a, 0x5f, 0x48, 0x36, 0x01, 0x44, 0x13, 0x60, 0x04, 0x9e, 0x5e, 0x3e, 0xc0, 0xde, 0x0a, 0x94, 0xd9, 0x0e, 0xd8, 0x7d, 0x82, 0xf5, 0xa5, 0x17, 0x48, 0x03}, {}, 0},
  {{0x75, 0x5c, 0x4c, 0xb9, 0x25, 0x6c, 0xa7, 0xcd, 0xc4, 0xac, 0xfd, 0xc6, 0xcf, 0xee, 0xda, 0x84, 0x90, 0x17, 0xe5, 0xb9, 0xf9, 0x51, 0x4e, 0x99, 0x19, 0x1b, 0xd6, 0x7e, 0x0b, 0x0d, 0x42, 0x76}, {0x66, 0x8d, 0x64, 0x88, 0x07, 0xb6, 0xf9, 0x92, 0x6d, 0x1f, 0x49, 0xb0, 0x41, 0x38, 0x2f, 0x4f, 0xf5, 0x4e, 0xc3, 0x67, 0x1b, 0x97, 0xd0, 0x59, 0x4b, 0x7f, 0xe3, 0xca, 0x19, 0x06, 0xe7, 0xf9, 0x27, 0x87, 0xb9, 0xbf, 0x62, 0x82, 0x0a, 0xe7, 0x9c, 0xf0, 0xed, 0xff, 0x5f, 0xa2, 0x6a, 0xa8, 0xa4, 0xdc, 0x88, 0xf8, 0x4e, 0xe1, 0x7d, 0xda, 0x1f, 0x67, 0x45, 0x5e, 0xa6, 0xc2, 0xd4, 0x09}, {0x61, 0x62, 0x63}, 3},
  {{0x75, 0x5c, 0x4c, 0xb9, 0x25, 0x6c, 0xa7, 0xcd, 0xc4, 0xac, 0xfd, 0xc6, 0xcf, 0xee, 0xda, 0x84, 0x90, 0x17, 0xe5, 0xb9, 0xf9, 0x51, 0x4e, 0x99, 0x19, 0x1b, 0xd6, 0x7e, 0x0b, 0x0d, 0x42, 0x76}, {0x47, 0x28, 0x0c, 0xc1, 0x5b, 0x4f, 0x47, 0x5e, 0xff, 0x27, 0xe9, 0x90, 0x1c, 0x13, 0x1a, 0x98, 0x35, 0xe9, 0xcd, 0xd1, 0xc0, 0xe1, 0x4c, 0xca, 0x78, 0x30, 0xa3, 0x5f, 0xd2, 0x43, 0x99, 0x10, 0xf7, 0xea, 0x5f, 0xcd, 0xd5, 0x11, 0xa0, 0x6a, 0x6b, 0x3e, 0xc7, 0xb3, 0x77, 0xf6, 0xdd, 0x7b, 0x11, 0x61, 0x53, 0xc1, 0xa5, 0xc5, 0x6a, 0xe3, 0xe3, 0xb1, 0x15, 0xd0, 0x6b, 0xe9, 0xe5, 0x07}, {0x61, 0x72, 0x6d, 0x6f, 0x72, 0x2d, 0x72, 0x61, 0x64, 0x61, 0x72, 0x2d, 0x30, 0x2e, 0x36, 0x2e, 0x36, 0x2d, 0x73, 0x33, 0x2d, 0x65, 0x74, 0x68}, 24},
  {{0x75, 0x5c, 0x4c, 0xb9, 0x25, 0x6c, 0xa7, 0xcd, 0xc4, 0xac, 0xfd, 0xc6, 0xcf, 0xee, 0xda, 0x84, 0x90, 0x17, 0xe5, 0xb9, 0xf9, 0x51, 0x4e, 0x99, 0x19, 0x1b, 0xd6, 0x7e, 0x0b, 0x0d, 0x42, 0x76}, {0x00, 0x32, 0x44, 0xa1, 0xb1, 0xd1, 0xfb, 0xea, 0x9c, 0x03, 0x03, 0xcf, 0x1c, 0x88, 0x1b, 0x3c, 0x3f, 0xbf, 0x45, 0x2b, 0xd1, 0x60, 0xf5, 0xd3, 0x61, 0xad, 0xc6, 0x8b, 0xce, 0x33, 0x38, 0x5d, 0xdc, 0xa4, 0xe6, 0x29, 0x82, 0x90, 0x86, 0xb8, 0x77, 0xe0, 0xc6, 0xea, 0x2a, 0x25, 0x05, 0x33, 0xaa, 0x2a, 0x89, 0x03, 0x99, 0xac, 0x39, 0xd7, 0xa6, 0x54, 0x35, 0x74, 0xfb, 0xc4, 0xe6, 0x03}, {0x00, 0x0d, 0x1a, 0x27, 0x34, 0x41, 0x4e, 0x5b, 0x68, 0x75, 0x82, 0x8f, 0x9c, 0xa9, 0xb6, 0xc3, 0xd0, 0xdd, 0xea, 0xf7, 0x04, 0x11, 0x1e, 0x2b, 0x38, 0x45, 0x52, 0x5f, 0x6c, 0x79, 0x86, 0x93, 0xa0, 0xad, 0xba, 0xc7, 0xd4, 0xe1, 0xee, 0xfb, 0x08, 0x15, 0x22, 0x2f, 0x3c, 0x49, 0x56, 0x63, 0x70, 0x7d, 0x8a, 0x97, 0xa4, 0xb1, 0xbe, 0xcb, 0xd8, 0xe5, 0xf2, 0xff, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4d, 0x5a, 0x67, 0x74, 0x81, 0x8e, 0x9b, 0xa8, 0xb5, 0xc2, 0xcf, 0xdc, 0xe9, 0xf6, 0x03, 0x10, 0x1d, 0x2a, 0x37, 0x44, 0x51, 0x5e, 0x6b, 0x78, 0x85, 0x92, 0x9f, 0xac, 0xb9, 0xc6, 0xd3, 0xe0, 0xed, 0xfa, 0x07, 0x14, 0x21, 0x2e, 0x3b, 0x48, 0x55, 0x62, 0x6f, 0x7c, 0x89, 0x96, 0xa3, 0xb0, 0xbd, 0xca, 0xd7, 0xe4, 0xf1, 0xfe, 0x0b, 0x18, 0x25, 0x32, 0x3f, 0x4c, 0x59, 0x66, 0x73, 0x80, 0x8d, 0x9a, 0xa7, 0xb4, 0xc1, 0xce, 0xdb, 0xe8, 0xf5, 0x02, 0x0f, 0x1c, 0x29, 0x36, 0x43, 0x50, 0x5d, 0x6a, 0x77, 0x84, 0x91, 0x9e, 0xab, 0xb8, 0xc5, 0xd2, 0xdf, 0xec, 0xf9, 0x06, 0x13, 0x20, 0x2d, 0x3a, 0x47, 0x54, 0x61, 0x6e, 0x7b, 0x88, 0x95, 0xa2, 0xaf, 0xbc, 0xc9, 0xd6, 0xe3, 0xf0, 0xfd, 0x0a, 0x17, 0x24, 0x31, 0x3e, 0x4b, 0x58, 0x65, 0x72, 0x7f, 0x8c, 0x99, 0xa6, 0xb3, 0xc0, 0xcd, 0xda, 0xe7, 0xf4, 0x01, 0x0e, 0x1b, 0x28, 0x35, 0x42, 0x4f, 0x5c, 0x69, 0x76, 0x83, 0x90, 0x9d, 0xaa, 0xb7, 0xc4, 0xd1, 0xde, 0xeb, 0xf8, 0x05, 0x12, 0x1f, 0x2c, 0x39, 0x46, 0x53, 0x60, 0x6d, 0x7a, 0x87, 0x94, 0xa1, 0xae, 0xbb, 0xc8, 0xd5, 0xe2, 0xef, 0xfc, 0x09, 0x16, 0x23, 0x30, 0x3d, 0x4a, 0x57, 0x64, 0x71, 0x7e, 0x8b, 0x98, 0xa5, 0xb2, 0xbf, 0xcc, 0xd9, 0xe6, 0xf3, 0x00, 0x0d, 0x1a, 0x27, 0x34, 0x41, 0x4e, 0x5b, 0x68, 0x75, 0x82, 0x8f, 0x9c, 0xa9, 0xb6, 0xc3, 0xd0, 0xdd, 0xea, 0xf7, 0x04, 0x11, 0x1e, 0x2b, 0x38, 0x45, 0x52, 0x5f, 0x6c, 0x79, 0x86, 0x93, 0xa0, 0xad, 0xba, 0xc7, 0xd4, 0xe1, 0xee, 0xfb, 0x08, 0x15, 0x22, 0x2f}, 300},
  {{0x75, 0x5c, 0x4c, 0xb9, 0x25, 0x6c, 0xa7, 0xcd, 0xc4, 0xac, 0xfd, 0xc6, 0xcf, 0xee, 0xda, 0x84, 0x90, 0x17, 0xe5, 0xb9, 0xf9, 0x51, 0x4e, 0x99, 0x19, 0x1b, 0xd6, 0x7e, 0x0b, 0x0d, 0x42, 0x76}, {0x5e, 0x5b, 0x82, 0x9b, 0xd3, 0xbe, 0xb1, 0x90, 0x2a, 0xe9, 0x61, 0x83, 0xd8, 0x00, 0xdc, 0x67, 0x24, 0x3e, 0xea, 0x87, 0x4f, 0x2d, 0x52, 0x39, 0x9a, 0x56, 0xcc, 0x40, 0x64, 0x3f, 0x36, 0x8b, 0xab, 0x8a, 0x1a, 0xd5, 0x5f, 0x74, 0xba, 0x20, 0xc9, 0x5c, 0xc7, 0x86, 0x5e, 0x11, 0x62, 0x4d, 0xaf, 0xde, 0x21, 0x9a, 0x9f, 0x5c, 0xad, 0x0c, 0x30, 0x8b, 0x03, 0xe3, 0xf5, 0x7b, 0xe7, 0x04}, {0xa5, 0xdf, 0x3f, 0xde, 0x7b, 0x20, 0x0c, 0x3f, 0xa2, 0x79, 0x11, 0x44, 0xc7, 0xe0, 0x5c, 0x5c, 0x2b, 0xce, 0xd4, 0xc6, 0xad, 0x86, 0x3d, 0x3a, 0x4c, 0xf7, 0x6c, 0x97, 0x2c, 0x17, 0x12, 0xf9}, 32},
};

static std::string hex_of(const std::uint8_t* data, std::size_t length) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < length; ++i) { out += digits[data[i] >> 4]; out += digits[data[i] & 15]; }
  return out;
}

static void test_ed25519() {
  std::uint8_t digest[64];
  armor::ed25519::sha512(nullptr, 0, digest);
  CHECK(hex_of(digest, 64) == "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
  armor::ed25519::sha512(reinterpret_cast<const std::uint8_t*>("abc"), 3, digest);
  CHECK(hex_of(digest, 64) == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  const std::string long_text(1000, 'a');   // several blocks, and the padding that spills into one more
  armor::ed25519::sha512(reinterpret_cast<const std::uint8_t*>(long_text.data()), long_text.size(), digest);
  CHECK(hex_of(digest, 64) == "67ba5535a46e3f86dbfbed8cbbaf0125c76ed549ff8b0b9e03e0c88cf90fa634fa7b12b47d77b694de488ace8d9a65967dc96df599727d3292a8d9d447709c97");
  for (const Ed25519Vector& v : kEd25519Vectors) {
    CHECK(armor::ed25519::verify(v.signature, v.message, v.length, v.public_key));
    // anything changed is refused: the message, the signature (R and S), the key
    std::uint8_t message[300];
    std::memcpy(message, v.message, sizeof message);
    message[v.length ? v.length - 1 : 0] ^= 1;
    if (v.length) CHECK(!armor::ed25519::verify(v.signature, message, v.length, v.public_key));
    else CHECK(!armor::ed25519::verify(v.signature, message, 1, v.public_key));
    for (int byte : {0, 17, 31, 32, 50, 62}) {
      std::uint8_t signature[64];
      std::memcpy(signature, v.signature, 64);
      signature[byte] ^= 4;
      CHECK(!armor::ed25519::verify(signature, v.message, v.length, v.public_key));
    }
    std::uint8_t key[32];
    std::memcpy(key, v.public_key, 32);
    key[5] ^= 8;
    CHECK(!armor::ed25519::verify(v.signature, v.message, v.length, key));
  }
  // a signature of one message is not the signature of another, and not under another key
  CHECK(!armor::ed25519::verify(kEd25519Vectors[0].signature, kEd25519Vectors[1].message, kEd25519Vectors[1].length, kEd25519Vectors[0].public_key));
  CHECK(!armor::ed25519::verify(kEd25519Vectors[0].signature, kEd25519Vectors[0].message, kEd25519Vectors[0].length, kEd25519Vectors[5].public_key));
  // S must be below the order of the group: S + L is the same point and used to verify as a second signature of the same message
  {
    static const std::uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
    for (const Ed25519Vector& v : kEd25519Vectors) {
      std::uint8_t malleable[64];
      std::memcpy(malleable, v.signature, 64);
      unsigned carry = 0;
      for (int i = 0; i < 32; ++i) {
        const unsigned sum = malleable[32 + i] + L[i] + carry;
        malleable[32 + i] = static_cast<std::uint8_t>(sum & 255);
        carry = sum >> 8;
      }
      CHECK(carry == 0 && (malleable[63] & 0xe0) == 0);   // it fits below 2^253, so only the order check can refuse it
      CHECK(armor::ed25519::verify(v.signature, v.message, v.length, v.public_key));
      CHECK(!armor::ed25519::verify(malleable, v.message, v.length, v.public_key));
    }
  }
  // the top bits of S must be clear, and a key that is not a point is refused
  std::uint8_t big[64];
  std::memcpy(big, kEd25519Vectors[0].signature, 64);
  big[63] |= 0xe0;
  CHECK(!armor::ed25519::verify(big, kEd25519Vectors[0].message, kEd25519Vectors[0].length, kEd25519Vectors[0].public_key));
  std::uint8_t not_a_point[32];
  std::memset(not_a_point, 0xff, 32);
  not_a_point[31] = 0x7f;
  CHECK(!armor::ed25519::verify(kEd25519Vectors[0].signature, kEd25519Vectors[0].message, kEd25519Vectors[0].length, not_a_point));
}

static void test_fw_trust() {
  // an image hash signed with the real key of the releases (made by tools outside the repository, with the private key that never leaves its owner)
  std::uint8_t digest[32];
  for (int i = 0; i < 32; ++i) digest[i] = static_cast<std::uint8_t>(i);
  const std::string good = "98a8684477408758e630a7c7450d31a5df5d3c7c1ecf8803c9dbf2576a38880bda9eadfbad9483d54338d0eb28ea3fba12c4e11790f08301b8bd25b3c705d108";
  CHECK(armor::fw_trust::image_signature_ok(digest, good));
  CHECK(armor::fw_trust::image_signature_ok(digest, good + "\n"));            // a line from a file
  CHECK(armor::fw_trust::image_signature_ok(digest, " " + good + "\r\n"));
  std::string upper = good;
  for (char& c : upper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 32);
  CHECK(armor::fw_trust::image_signature_ok(digest, upper));                   // either case
  std::uint8_t other[32];
  std::memcpy(other, digest, 32);
  other[31] ^= 1;
  CHECK(!armor::fw_trust::image_signature_ok(other, good));                    // another image
  std::string changed = good;
  changed[10] = changed[10] == '0' ? '1' : '0';
  CHECK(!armor::fw_trust::image_signature_ok(digest, changed));                // a changed signature
  CHECK(!armor::fw_trust::image_signature_ok(digest, ""));                     // no signature
  CHECK(!armor::fw_trust::image_signature_ok(digest, good.substr(0, 126)));    // a short one
  CHECK(!armor::fw_trust::image_signature_ok(digest, good + "00"));            // a long one
  std::string not_hex = good;
  not_hex[20] = 'g';
  CHECK(!armor::fw_trust::image_signature_ok(digest, not_hex));
  CHECK(!armor::fw_trust::image_signature_ok(digest, std::string(128, '0')));  // nothing signed
}

static void test_ble_passkey() {
  const std::uint32_t a = armor::auth::ble_passkey_from("ABCDEFGH23"), b = armor::auth::ble_passkey_from("ABCDEFGH24");
  CHECK(a < 1000000 && b < 1000000);
  CHECK(a == armor::auth::ble_passkey_from("ABCDEFGH23"));   // the same code, the same PIN: the node keeps nothing more
  CHECK(a != b);                                              // another code, another PIN
  int different = 0;
  for (char c = 'A'; c < 'Z'; ++c) if (armor::auth::ble_passkey_from(std::string("CODE") + c) != armor::auth::ble_passkey_from("CODEA")) ++different;
  CHECK(different >= 23);                                     // spread, not a pattern
}

static void test_backoff() {
  retry::Backoff plain(1000, 30000, 0);   // no scatter: the schedule itself
  const std::uint32_t expected[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000};
  for (const std::uint32_t wait : expected) CHECK(plain.next(12345) == wait);
  CHECK(plain.failures() >= 8);
  plain.reset();
  CHECK(plain.failures() == 0 && plain.next(1) == 1000);   // a try that worked starts the schedule over
  // with scatter: within 20 % of the plain wait, never above the ceiling, never zero
  retry::Backoff scattered(1000, 30000, 20);
  for (std::uint32_t random : {0u, 1u, 20u, 40u, 41u, 999u, 4294967295u}) {
    retry::Backoff b(1000, 30000, 20);
    b.next(random); b.next(random);                       // two failures: the third wait is 4000 ms
    const std::uint32_t wait = b.next(random);
    CHECK(wait >= 3200 && wait <= 4800);
  }
  for (int i = 0; i < 40; ++i) { const std::uint32_t wait = scattered.next(static_cast<std::uint32_t>(i) * 2654435761u); CHECK(wait >= 1 && wait <= 30000); }
  // odd settings are made sensible: a ceiling under the first wait is the first wait, a first wait of zero is one millisecond
  retry::Backoff odd(0, 0, 500);
  CHECK(odd.next(7) >= 1 && odd.next(7) <= 2);
  // many nodes that lost the same router at the same moment do not wait the same: different random numbers give different waits
  retry::Backoff a(1000, 60000, 20), b(1000, 60000, 20);
  a.next(1); b.next(2);
  CHECK(a.next(3) != b.next(9));
}

// ---- Improv Wi-Fi over serial ---------------------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> rpc_wifi(const std::string& ssid, const std::string& password) {
  std::vector<std::uint8_t> data{0x01, static_cast<std::uint8_t>(2 + ssid.size() + password.size()), static_cast<std::uint8_t>(ssid.size())};
  data.insert(data.end(), ssid.begin(), ssid.end());
  data.push_back(static_cast<std::uint8_t>(password.size()));
  data.insert(data.end(), password.begin(), password.end());
  return improv::packet(improv::Type::kRpc, data);
}

static std::optional<improv::Packet> feed_all(improv::Parser& parser, const std::vector<std::uint8_t>& bytes) {
  std::optional<improv::Packet> last;
  for (const std::uint8_t b : bytes) if (auto p = parser.feed(b)) last = p;
  return last;
}

static void test_improv_packets() {
  // the state packet of the standard: IMPROV, version 1, type 1, length 1, the state, the checksum and a newline
  const std::vector<std::uint8_t> ready = improv::state_packet(improv::State::kReady);
  const std::vector<std::uint8_t> known{'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x01, 0x01, 0x02, 0x00, '\n'};
  std::vector<std::uint8_t> sum_of(known.begin(), known.begin() + 10);
  CHECK(improv::checksum(sum_of) == static_cast<std::uint8_t>((73 + 77 + 80 + 82 + 79 + 86 + 1 + 1 + 1 + 2) & 0xFF));
  CHECK(ready.size() == 12 && ready[7] == 0x01 && ready[8] == 1 && ready[9] == 0x02 && ready.back() == '\n' && ready[10] == improv::checksum(sum_of));
  const std::vector<std::uint8_t> error = improv::error_packet(improv::Error::kUnableToConnect);
  CHECK(error[7] == 0x02 && error[9] == 0x03);
  // an RPC result: command, the length of what follows, then each string with its length in front
  const std::vector<std::uint8_t> result = improv::result_packet(improv::Command::kGetDeviceInfo, {"ab", "c"});
  CHECK(result[7] == 0x04 && result[8] == 7 && result[9] == 0x04 && result[10] == 5 && result[11] == 2 && result[12] == 'a' && result[13] == 'b' && result[14] == 1 && result[15] == 'c');
  const std::vector<std::uint8_t> empty = improv::result_packet(improv::Command::kWifiSettings, {});
  CHECK(empty[8] == 2 && empty[9] == 0x01 && empty[10] == 0);
  // a packet never carries more than 255 bytes of data
  const std::vector<std::uint8_t> big = improv::packet(improv::Type::kRpcResult, std::vector<std::uint8_t>(400, 'x'));
  CHECK(big[8] == 255 && big.size() == 9 + 255 + 2);
}

static void test_improv_parser() {
  improv::Parser parser;
  // the Wi-Fi settings, with noise (log lines, a person typing) before and between packets
  std::vector<std::uint8_t> stream;
  for (const char c : std::string("I (123) armor-net: link up\r\nIMP garbage IMPROV\r\n")) stream.push_back(static_cast<std::uint8_t>(c));
  const std::vector<std::uint8_t> wifi = rpc_wifi("HomeRouter", "una-clave-larga");
  stream.insert(stream.end(), wifi.begin(), wifi.end());
  const std::optional<improv::Packet> got = feed_all(parser, stream);
  CHECK(got.has_value() && got->type == improv::Type::kRpc);
  improv::Rpc rpc;
  CHECK(got && improv::parse_rpc(got->data, rpc) == improv::Error::kNone && rpc.command == improv::Command::kWifiSettings && rpc.ssid == "HomeRouter" && rpc.password == "una-clave-larga");
  // a wrong checksum is dropped and counted, and the next good packet still arrives
  std::vector<std::uint8_t> broken = rpc_wifi("a", "b");
  broken[broken.size() - 2] ^= 0x55;
  const std::uint32_t before = parser.rejected();
  CHECK(!feed_all(parser, broken).has_value() && parser.rejected() == before + 1);
  CHECK(feed_all(parser, rpc_wifi("a", "b")).has_value());
  // a version that is not 1 is not ours
  std::vector<std::uint8_t> future = rpc_wifi("a", "b");
  future[6] = 0x02;
  CHECK(!feed_all(parser, future).has_value());
  CHECK(feed_all(parser, rpc_wifi("c", "")).has_value());
  // a packet fed one byte at a time and one cut in the middle by another header
  improv::Parser slow;
  std::optional<improv::Packet> seen;
  for (const std::uint8_t b : rpc_wifi("x", "y")) if (auto p = slow.feed(b)) seen = p;
  CHECK(seen.has_value());
  improv::Parser cut;
  std::vector<std::uint8_t> half = rpc_wifi("abc", "def");
  half.resize(11);
  const std::vector<std::uint8_t> whole = rpc_wifi("zz", "yy");
  half.insert(half.end(), whole.begin(), whole.end());
  const std::optional<improv::Packet> after_cut = feed_all(cut, half);
  CHECK(after_cut.has_value() || cut.rejected() >= 1);   // the cut one is lost; the stream is not wedged
  CHECK(feed_all(cut, rpc_wifi("q", "r")).has_value());
  // an empty stream and a lone byte do nothing
  improv::Parser nothing;
  CHECK(!nothing.feed('I') && !nothing.feed('x') && !nothing.feed(0));
}

static void test_improv_rpc() {
  improv::Rpc rpc;
  CHECK(improv::parse_rpc({0x02, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kIdentify);
  CHECK(improv::parse_rpc({0x03, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kGetCurrentState);
  CHECK(improv::parse_rpc({0x04, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kGetDeviceInfo);
  CHECK(improv::parse_rpc({0x05, 0x00}, rpc) == improv::Error::kNone && rpc.command == improv::Command::kGetWifiNetworks);
  CHECK(improv::parse_rpc({0x77, 0x00}, rpc) == improv::Error::kUnknownRpc);
  CHECK(improv::parse_rpc({}, rpc) == improv::Error::kInvalidRpc && improv::parse_rpc({0x02}, rpc) == improv::Error::kInvalidRpc);
  CHECK(improv::parse_rpc({0x02, 0x05}, rpc) == improv::Error::kInvalidRpc);   // the length does not match the data
  // the Wi-Fi settings: the lengths must add up, the SSID is 1 to 32 bytes and the password at most 63
  CHECK(improv::parse_rpc({0x01, 0x03, 0x01, 'a', 0x00}, rpc) == improv::Error::kNone && rpc.ssid == "a" && rpc.password.empty());
  CHECK(improv::parse_rpc({0x01, 0x03, 0x05, 'a', 0x00}, rpc) == improv::Error::kInvalidRpc);          // the SSID says 5 bytes and has 1
  CHECK(improv::parse_rpc({0x01, 0x03, 0x00, 'a', 0x00}, rpc) == improv::Error::kInvalidRpc);          // an empty SSID
  std::vector<std::uint8_t> long_ssid{0x01, 0x00, 33};
  long_ssid.insert(long_ssid.end(), 33, 'a'); long_ssid.push_back(0); long_ssid[1] = static_cast<std::uint8_t>(long_ssid.size() - 2);
  CHECK(improv::parse_rpc(long_ssid, rpc) == improv::Error::kInvalidRpc);
  std::vector<std::uint8_t> long_pass{0x01, 0x00, 1, 'a', 64};
  long_pass.insert(long_pass.end(), 64, 'p'); long_pass[1] = static_cast<std::uint8_t>(long_pass.size() - 2);
  CHECK(improv::parse_rpc(long_pass, rpc) == improv::Error::kInvalidRpc);
  CHECK(improv::parse_rpc({0x01, 0x01, 0x00}, rpc) == improv::Error::kInvalidRpc);
}

// ---- the stack report -----------------------------------------------------------------------------------------------------------------------

// ---- the health report every node says about itself -----------------------------------------------------------------------------------

static nodehealth::Report sample_report() {
  nodehealth::Report r;
  r.node_id = "alarm-1"; r.family = "alarm"; r.firmware = "0.3.6"; r.board = "s3-eth"; r.timestamp_ms = 9000; r.uptime_s = 86400; r.reset_reason = "power_on";
  r.heap_free = 142000; r.heap_min = 98000; r.link = {nodehealth::Link::kEthernet, true, 0}; r.broker_reconnects = 1; r.low_stack_tasks = 0;
  return r;
}

static void test_node_health() {
  using nodehealth::Link;
  // the reset reasons of ESP-IDF, in the words of the contract
  CHECK(std::string(nodehealth::reset_reason_text(1)) == "power_on" && std::string(nodehealth::reset_reason_text(3)) == "software" && std::string(nodehealth::reset_reason_text(4)) == "panic");
  CHECK(std::string(nodehealth::reset_reason_text(5)) == "int_wdt" && std::string(nodehealth::reset_reason_text(6)) == "task_wdt" && std::string(nodehealth::reset_reason_text(9)) == "brownout");
  CHECK(std::string(nodehealth::reset_reason_text(0)) == "unknown" && std::string(nodehealth::reset_reason_text(99)) == "unknown" && std::string(nodehealth::reset_reason_text(-1)) == "unknown");
  CHECK(nodehealth::is_abnormal_reset("panic") && nodehealth::is_abnormal_reset("task_wdt") && nodehealth::is_abnormal_reset("brownout") && !nodehealth::is_abnormal_reset("power_on") && !nodehealth::is_abnormal_reset("software") && !nodehealth::is_abnormal_reset("external"));
  // which link the node is on
  const nodehealth::LinkInfo cable = nodehealth::choose_link(true, true, true, false, 0);
  CHECK(cable.link == Link::kEthernet && cable.up);
  const nodehealth::LinkInfo cable_no_ip = nodehealth::choose_link(true, true, false, false, 0);
  CHECK(cable_no_ip.link == Link::kEthernet && !cable_no_ip.up);
  const nodehealth::LinkInfo wifi = nodehealth::choose_link(false, true, true, true, -61);
  CHECK(wifi.link == Link::kWifi && wifi.up && wifi.rssi_dbm == -61);
  const nodehealth::LinkInfo wifi_board_down = nodehealth::choose_link(false, false, false, false, 0);
  CHECK(wifi_board_down.link == Link::kWifi && !wifi_board_down.up);
  const nodehealth::LinkInfo cable_board_down = nodehealth::choose_link(true, false, false, false, 0);
  CHECK(cable_board_down.link == Link::kEthernet && !cable_board_down.up);
  CHECK(nodehealth::choose_link(true, false, true, true, -70).link == Link::kWifi);   // a board with a cable and no carrier, joined by Wi-Fi
  CHECK(nodehealth::choose_link(false, true, true, true, 12).rssi_dbm == 0 && nodehealth::choose_link(false, true, true, true, -300).rssi_dbm == -120);
  // the last error of a log
  std::string text;
  CHECK(nodehealth::error_text_of_line("E (12345) armor-mqtt: the MQTT client could not be created", text) && text == "armor-mqtt: the MQTT client could not be created");
  CHECK(nodehealth::error_text_of_line("\x1b[0;31mE (7) tag: coloured\x1b[0m\r\n", text) && text == "tag: coloured");
  CHECK(!nodehealth::error_text_of_line("W (12345) tag: only a warning", text) && !nodehealth::error_text_of_line("I (1) tag: info", text) && !nodehealth::error_text_of_line("", text) && !nodehealth::error_text_of_line("E (1) ", text));
  CHECK(!nodehealth::error_text_of_line("Error: not the format", text) && !nodehealth::error_text_of_line("E (123456789012345678) tag: too long a stamp", text));
  CHECK(nodehealth::error_text_of_line("E (1) tag: " + std::string(300, 'x'), text) && text.size() == nodehealth::kMaxErrorChars);
  CHECK(nodehealth::error_text_of_line(std::string("E (1) tag: caf") + static_cast<char>(0xC3) + static_cast<char>(0xA9), text) && text == "tag: caf??");   // only printable ASCII goes out
  const std::string log = "I (1) a: hello\nE (2) a: first\nW (3) a: warn\nE (4) b: second\nI (5) a: bye\n";
  CHECK(nodehealth::last_error_of_log(log) == "b: second" && nodehealth::last_error_of_log("I (1) a: fine\n") == "" && nodehealth::last_error_of_log("") == "");
  CHECK(nodehealth::last_error_of_log("E (2) a: no newline at the end") == "a: no newline at the end");
  // the message
  CHECK(nodehealth::topic("alarm-1") == "armor/health/alarm-1" && nodehealth::topic("Bad Id").empty() && nodehealth::topic("").empty());
  CHECK(nodehealth::valid_family("radar") && nodehealth::valid_family("hmi") && !nodehealth::valid_family("panel") && !nodehealth::valid_family(""));
  const std::string wired = nodehealth::to_json(sample_report());
  CHECK(wired == "{\"kind\":\"node_health\",\"node_id\":\"alarm-1\",\"timestamp_ms\":9000,\"family\":\"alarm\",\"firmware\":\"0.3.6\",\"board\":\"s3-eth\",\"uptime_s\":86400,\"reset_reason\":\"power_on\","
                  "\"heap_free\":142000,\"heap_min\":98000,\"link\":\"ethernet\",\"link_up\":true,\"broker_reconnects\":1,\"low_stack_tasks\":0}");
  nodehealth::Report on_wifi = sample_report();
  on_wifi.link = {Link::kWifi, true, -61};
  on_wifi.last_error = "mqtt: refused";
  const std::string wifi_json = nodehealth::to_json(on_wifi);
  CHECK(contains(wifi_json, "\"link\":\"wifi\",\"link_up\":true,\"rssi_dbm\":-61,") && contains(wifi_json, "\"last_error\":\"mqtt: refused\""));
  nodehealth::Report down = sample_report();
  down.link = {Link::kWifi, false, -61};
  CHECK(!contains(nodehealth::to_json(down), "rssi_dbm") && contains(nodehealth::to_json(down), "\"link_up\":false"));   // no signal strength while the link is down
  nodehealth::Report none = sample_report();
  none.link = {Link::kNone, true, 0};
  CHECK(contains(nodehealth::to_json(none), "\"link\":\"none\",\"link_up\":false"));                                    // no link is a link that is down
  nodehealth::Report cable_rssi = sample_report();
  cable_rssi.link = {Link::kEthernet, true, -50};
  CHECK(!contains(nodehealth::to_json(cable_rssi), "rssi_dbm"));
  nodehealth::Report odd = sample_report();
  odd.heap_min = 999999; odd.low_stack_tasks = 500; odd.board = "";
  const std::string odd_json = nodehealth::to_json(odd);
  CHECK(contains(odd_json, "\"heap_min\":142000") && contains(odd_json, "\"low_stack_tasks\":64") && !contains(odd_json, "\"board\""));   // never more than the contract allows
  json::Value parsed;
  CHECK(json::parse(wired, parsed) && parsed.is_object() && parsed.string_or("kind", "") == "node_health");
}

static void test_task_health() {
  const std::vector<health::TaskInfo> tasks{{"main", 2048, 1, 'B'}, {"meters", 300, 5, 'B'}, {"mqtt", 300, 5, 'r'}, {"idle", 900, 0, 'R'}};
  CHECK(health::low_count(tasks) == 2 && health::is_low(tasks[1]) && !health::is_low(tasks[0]) && !health::is_low({"edge", 512, 0, 'B'}) && health::is_low({"edge", 511, 0, 'B'}));
  const std::string text = health::tasks_json(tasks);
  // the least room first, a tie by name
  CHECK(text.find("\"meters\"") < text.find("\"mqtt\"") && text.find("\"mqtt\"") < text.find("\"idle\"") && text.find("\"idle\"") < text.find("\"main\""));
  CHECK(contains(text, "{\"name\":\"meters\",\"stack_free\":300,\"priority\":5,\"state\":\"B\",\"low\":true}") && contains(text, "\"name\":\"main\",\"stack_free\":2048,\"priority\":1,\"state\":\"B\",\"low\":false"));
  CHECK(health::tasks_json({}) == "[]" && health::low_count({}) == 0);
}

// ---- the sessions of the panel ------------------------------------------------------------------------------------------------------------

static void test_remembered_session_is_not_taken_after_half_an_hour() {
  using namespace armor::auth;
  SessionTable table;
  const std::string phone(40, 'p');
  table.create(phone, "admin", Role::kAdmin, 1000, /*remembered=*/true);
  // five more people come in 31 minutes later and fill the table (six places); the phone's session is remembered, so it is not idle for thirty days
  const std::uint64_t later = 1000 + 31ULL * 60ULL * 1000ULL;
  for (int i = 0; i < 5; ++i) table.create(std::string(40, static_cast<char>('a' + i)), "user", Role::kAdmin, later);
  CHECK(table.touch(phone, later + 1) != nullptr);      // it is still there
  // a plain session that has been idle too long is the one that goes when the table is full
  SessionTable full;
  for (int i = 0; i < 6; ++i) full.create(std::string(40, static_cast<char>('a' + i)), "user", Role::kAdmin, 1000);
  full.create(std::string(40, 'z'), "new", Role::kAdmin, 1000 + 31ULL * 60ULL * 1000ULL);
  CHECK(full.touch(std::string(40, 'z'), 1000 + 31ULL * 60ULL * 1000ULL + 1) != nullptr);
  // with nothing idle, the one idle the longest is replaced
  SessionTable busy;
  for (int i = 0; i < 6; ++i) busy.create(std::string(40, static_cast<char>('a' + i)), "user", Role::kAdmin, 1000 + static_cast<std::uint64_t>(i) * 10);
  busy.create(std::string(40, 'z'), "new", Role::kAdmin, 2000);
  CHECK(busy.touch(std::string(40, 'a'), 2001) == nullptr && busy.touch(std::string(40, 'b'), 2001) != nullptr);
}

static void test_json_strictness() {
  using namespace armor;
  json::Value document;
  // a name repeated in an object is refused: the server's parser keeps the last one and this one the first, so a text could mean two things
  CHECK(!json::parse("{\"a\":1,\"a\":2}", document));
  CHECK(!json::parse("{\"x\":{\"b\":true,\"b\":false}}", document));
  CHECK(!json::parse("[{\"a\":1,\"a\":1}]", document));
  CHECK(json::parse("{\"a\":1,\"b\":{\"a\":2}}", document) && document.get("a")->number == 1);   // the same name at another level is not a repeat
  // a whole number is written whole
  json::Writer w;
  w.begin_array().number(1234567890.0).number(5.0).number(0.25).number(-7.0).number(1e15).number(1e300).end_array();
  CHECK(w.str().rfind("[1234567890,5,0.25,-7,1000000000000000,", 0) == 0);
  json::Value back;
  CHECK(json::parse(w.str(), back) && back.items[0].number == 1234567890.0);
  // the id of a request of the Bluetooth channel is a whole number that fits
  ble::Request request;
  CHECK(ble::parse_request("{\"id\":7,\"op\":\"config.get\"}", request) && request.id == 7);
  CHECK(!ble::parse_request("{\"id\":1e300,\"op\":\"config.get\"}", request));
  CHECK(!ble::parse_request("{\"id\":9007199254740993,\"op\":\"config.get\"}", request));
  CHECK(!ble::parse_request("{\"id\":-1,\"op\":\"config.get\"}", request) && !ble::parse_request("{\"id\":1.5,\"op\":\"config.get\"}", request));
}

static void test_login_throttle_keeps_its_blocks() {
  using namespace armor::auth;
  LoginThrottle throttle;
  // eight addresses get blocked ...
  for (std::uint32_t i = 0; i < 8; ++i) for (int j = 0; j < 5; ++j) throttle.failure(100 + i, 1000 + j);
  for (std::uint32_t i = 0; i < 8; ++i) CHECK(!throttle.allowed(100 + i, 2000));
  // ... a ninth address does not push one of them out to make room: it waits too, and the blocks stand
  CHECK(!throttle.allowed(900, 2000) && throttle.wait_s(900, 2000) > 0);
  for (int j = 0; j < 5; ++j) throttle.failure(900, 2001 + j);   // (a caller that did not look at allowed() gains nothing)
  for (std::uint32_t i = 0; i < 8; ++i) CHECK(!throttle.allowed(100 + i, 2000));
  // when a block ends the place is free again, for the address that is waiting
  CHECK(throttle.allowed(900, 1004 + 30000) && throttle.wait_s(900, 1004 + 30000) == 0);
  throttle.failure(900, 1004 + 30000);
  CHECK(throttle.allowed(900, 1004 + 30001));
  // an address that was only failing a little is the one that goes when the table is full
  LoginThrottle small;
  for (std::uint32_t i = 0; i < 8; ++i) small.failure(200 + i, 10 + i);
  small.failure(300, 100);
  CHECK(small.allowed(200, 101) && small.allowed(300, 101));
}

// A phone that fails is blocked on its own; everybody is blocked only by many failures together.
namespace {
class StubBackend : public armor::ble::Backend {
 public:
  bool has_users() override { return true; }
  std::string hello_json() override { return "{}"; }
  bool setup_code_ok(std::string_view) override { return false; }
  armor::auth::Result add_administrator(std::string_view, std::string_view) override { return armor::auth::Result::kStorage; }
  bool verify(std::string_view user, std::string_view password, armor::auth::Role& role) override { role = armor::auth::Role::kAdmin; return user == "admin" && password == "right-password"; }
  std::string config_json() override { return "{}"; }
  int config_put(std::string_view, std::string&) override { return 0; }
  std::string status_json() override { return "{}"; }
  bool wifi_scan(std::string&, std::string&) override { return false; }
  void restart_soon() override {}
  std::uint64_t now_ms() override { return now; }
  std::uint64_t now = 1000;
};
armor::ble::Request login(const std::string& password, long long id = 1) {
  armor::ble::Request request;
  request.id = id;
  request.op = "login";
  armor::json::Value args;
  armor::json::parse("{\"user\":\"admin\",\"password\":\"" + password + "\"}", args);
  request.args = args;
  return request;
}
}  // namespace

static void test_ble_throttle_per_phone() {
  using namespace armor;
  StubBackend backend;
  auth::LoginThrottle throttle;
  ble::Session attacker, owner;
  attacker.peer = 111;
  owner.peer = 222;
  for (int i = 0; i < 5; ++i) CHECK(contains(ble::handle(backend, attacker, throttle, login("wrong")), "wrong_credentials"));
  CHECK(contains(ble::handle(backend, attacker, throttle, login("right-password")), "too_many_attempts"));   // the phone that failed five times waits
  CHECK(contains(ble::handle(backend, owner, throttle, login("right-password")), "\"ok\":true"));          // the owner's phone does not
  // many phones failing together do block everybody, but only after twenty failures (a right login forgives them, so the count starts from nothing here)
  StubBackend crowd_backend;
  auth::LoginThrottle crowd;
  ble::Session many;
  for (std::uint32_t phone = 1000; phone < 1019; ++phone) { many.peer = phone; ble::handle(crowd_backend, many, crowd, login("wrong")); }
  ble::Session fresh;
  fresh.peer = 5000;
  CHECK(!contains(ble::handle(crowd_backend, fresh, crowd, login("wrong")), "too_many_attempts"));       // nineteen failures: still open (this is the twentieth try)
  ble::Session late;
  late.peer = 6000;
  CHECK(contains(ble::handle(crowd_backend, late, crowd, login("right-password")), "too_many_attempts"));  // the twentieth blocks everybody
  // a phone that is not known keeps the old rule: five failures block the channel
  StubBackend other;
  auth::LoginThrottle shared;
  ble::Session anonymous;
  for (int i = 0; i < 5; ++i) ble::handle(other, anonymous, shared, login("wrong"));
  CHECK(contains(ble::handle(other, anonymous, shared, login("right-password")), "too_many_attempts"));
  // the peer survives a clear (a logout must not turn the phone into an unknown one)
  attacker.clear();
  CHECK(attacker.peer == 111);
}

#ifdef ARMOR_HAS_BROKER_URI
static void test_broker_uri() {
  using armor::broker_uri::parse;
  CHECK(parse("mqtt://192.168.0.180:1883").ok && parse("mqtt://192.168.0.180:1883").host == "192.168.0.180" && parse("mqtt://192.168.0.180:1883").port == 1883);
  CHECK(parse("mqtt://broker.local").port == 1883 && parse("mqtts://broker.local").port == 8883 && parse("ws://h/mqtt").port == 80 && parse("wss://h").port == 443);
  CHECK(parse("mqtts://broker.local:8884/path?x=1").port == 8884 && parse("mqtts://broker.local:8884/path?x=1").host == "broker.local");
  CHECK(parse("mqtt://user:secret@host:1884").host == "host" && parse("mqtt://user:secret@host:1884").port == 1884);
  CHECK(parse("mqtt://[::1]:1884").host == "::1" && parse("mqtt://[::1]:1884").port == 1884 && parse("mqtt://[fe80::1]").port == 1883);
  CHECK(!parse("").ok && !parse("192.168.0.1:1883").ok && !parse("http://host").ok && !parse("mqtt://").ok && !parse("mqtt://:1883").ok);
  CHECK(!parse("mqtt://host:0").ok && !parse("mqtt://host:70000").ok && !parse("mqtt://host:12ab").ok && !parse("mqtt://host:").ok && !parse("mqtt://[::1").ok && !parse("mqtt://[::1]x").ok);
}
#endif  // ARMOR_HAS_BROKER_URI


// ---- the networks that may use the panel -------------------------------------------------------------------------------------------------

static std::uint32_t ip(const char* text) { std::uint32_t value = 0; const bool ok = adminsrc::parse_address(text, value); CHECK(ok); return value; }

static void test_user_key() {
  CHECK(auth::user_key("admin") == auth::user_key("ADMIN") && auth::user_key("admin") == auth::user_key("Admin"));
  CHECK(auth::user_key("admin") != auth::user_key("admin2") && auth::user_key("") != 0 && auth::user_key("admin") != 0);
  // wrong passwords for one person block that person from every address, and another person is not touched
  auth::LoginThrottle by_person;
  for (int i = 0; i < 5; ++i) by_person.failure(auth::user_key("admin"), 1000);
  CHECK(!by_person.allowed(auth::user_key("Admin"), 1001));
  CHECK(by_person.allowed(auth::user_key("visitor"), 1001));
  by_person.success(auth::user_key("admin"));
  CHECK(by_person.allowed(auth::user_key("admin"), 1002));
}

static void test_admin_sources() {
  using adminsrc::allowed;
  { std::uint32_t unused = 0; CHECK(!adminsrc::parse_address("010.1.1.1", unused) && !adminsrc::parse_address("1.2.3.04", unused) && !adminsrc::parse_address("00.1.1.1", unused) && adminsrc::parse_address("0.1.1.0", unused)); }   // same rule as net::parse_ipv4
  // the default is the house: the private ranges, the link-local ones, the loopback and the range of Tailscale; the Internet is not
  for (const char* inside : {"10.1.2.3", "172.16.0.1", "172.31.255.254", "192.168.0.50", "169.254.3.4", "127.0.0.1", "100.64.0.9", "100.127.255.1"}) CHECK(allowed(ip(inside), ""));
  for (const char* outside : {"8.8.8.8", "203.0.113.9", "172.15.0.1", "172.32.0.1", "192.169.0.1", "11.0.0.1"}) CHECK(!allowed(ip(outside), ""));
  CHECK(!allowed(0, ""));   // an address the node could not read is never allowed
  CHECK(allowed(ip("192.168.0.50"), "  "));
  // a list replaces the default: only what it names
  CHECK(allowed(ip("203.0.113.9"), "203.0.113.9"));
  CHECK(!allowed(ip("203.0.113.10"), "203.0.113.9"));
  CHECK(allowed(ip("192.168.1.77"), "192.168.1.0/24, 10.66.0.0/24"));
  CHECK(!allowed(ip("192.168.2.77"), "192.168.1.0/24, 10.66.0.0/24"));
  CHECK(allowed(ip("10.66.0.2"), "192.168.1.0/24,10.66.0.0/24"));
  CHECK(allowed(ip("192.168.1.77"), "192.168.1.5/24"));   // the base is cut to the prefix
  CHECK(allowed(ip("1.2.3.4"), "0.0.0.0/0"));            // everybody, if the owner wants it
  CHECK(!allowed(ip("192.168.0.50"), "203.0.113.9"));    // naming an outside address does not keep the house
  // what is not a list is refused, and nothing is let in by a list that cannot be read
  for (const char* bad : {"192.168.0", "192.168.0.256", "192.168.0.1/33", "192.168.0.1/", "192.168.0.1/-1", "a.b.c.d", "192.168.0.1,,10.0.0.0/8", ",10.0.0.0/8", "10.0.0.0/8,", "10.0.0.1.1", "10.0.0.1 /8", "192.168.0.1/ab"}) CHECK(!adminsrc::valid_list(bad));
  CHECK(adminsrc::valid_list("") && adminsrc::valid_list("10.0.0.0/8") && adminsrc::valid_list(" 10.0.0.0/8 , 192.168.0.1 "));
  CHECK(!adminsrc::valid_list("203.0.113.1,203.0.113.2,203.0.113.3,203.0.113.4,203.0.113.5,203.0.113.6,203.0.113.7,203.0.113.8,203.0.113.9"));   // nine entries: too many
  CHECK(!adminsrc::valid_list(std::string(130, '1')));
  CHECK(allowed(ip("192.168.0.50"), "garbage") && !allowed(ip("8.8.8.8"), "garbage"));   // a list that cannot be read falls back to the house, never to "everybody"
}

int main() {
  test_admin_sources();
  test_user_key();
  test_ble_throttle_per_phone();
#ifdef ARMOR_HAS_BROKER_URI
  test_broker_uri();
#endif
  test_json_strictness();
  test_login_throttle_keeps_its_blocks();
  test_ed25519();
  test_fw_trust();
  test_ble_passkey();
  test_remembered_session_is_not_taken_after_half_an_hour();
  test_backoff();
  test_improv_packets();
  test_improv_parser();
  test_improv_rpc();
  test_task_health();
  test_node_health();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
