// ARMOR-NODE-ELECTRICAL - the key that seals the secrets the node keeps in flash (core/secret_seal.hpp), and the choice of what is sealed.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Off by default. When the option is on (the node's own setting security.seal_secrets, or the server's request) the passwords, tokens, keys, PINs and password hashes the node stores are written sealed; everything
// else stays readable. The key is a record in the partition "nvs_keys" of the same flash chip: no eFuse is burned and the flash is not encrypted, so it does not protect against someone who reads the whole chip.
// A node installed with a partition table from before this option has no such partition: it says so and keeps its secrets as it always did.
#pragma once
#include <string>

namespace armor::vault {

// Reads the key record from its partition, if there is one. Call once, before the store reads anything.
void load();

bool partition_exists();   // this node's partition table has the key partition
bool have_key();           // a good key is held

// What the node is asked to do: its own setting and the server's request (the policy message of the alarm node); either one asks for sealing.
void set_own(bool on);
void set_server_request(bool on);
bool server_request();
bool wanted();
// Sealing is happening: wanted and a key is held.
bool active();

// Makes the key (random, written to the partition and read back) when there is none; false when the partition is missing or could not be written. Drops it (erases the partition) when nothing needs it any more.
bool create_key();
bool drop_key();

// The stored values that are secrets: "config" (the settings: only the secret members), and "users", "setup_code", "config_snap" and "tls_key" (the whole value).
bool is_secret_value(const char* name);
// Whether a value, as it is stored, is sealed (for "config": whether any member is).
bool stored_sealed(const char* name, const std::string& stored);
// A value as it was stored, with its seal taken off if it has one. `ok` is false when it is sealed and cannot be opened (no key, or not this key): the text returned is then empty.
std::string reveal(const char* name, const std::string& stored, bool& ok);
// A value ready to be stored: sealed when `seal_it` and the value is a secret one and a key is held; as it is otherwise.
std::string conceal(const char* name, const std::string& plain, bool seal_it);

}  // namespace armor::vault
