// ARMOR-ELECTRICAL - reading the settings kept in flash after an update. Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "json.hpp"

namespace armor::config {
namespace detail {
// Removes the member or the array item at `path` ("ports.2.baud") from a document. When that exact place is not there (a problem about a value that is missing), the enclosing object or item goes instead,
// and so on outwards, so that every problem leads to something that can be dropped. False when nothing at all could be.
inline bool drop_path(json::Value& root, std::string path) {
  while (!path.empty()) {
    json::Value* node = &root;
    bool dropped = false;
    std::size_t start = 0;
    for (;;) {
      const std::size_t dot = path.find('.', start);
      const bool last = dot == std::string::npos;
      const std::string part = path.substr(start, last ? std::string::npos : dot - start);
      std::size_t index = node->items.size();
      if (node->is_object()) {
        for (std::size_t i = 0; i < node->names.size(); ++i) if (node->names[i] == part) { index = i; break; }
      } else if (node->is_array() && !part.empty() && part.size() <= 6 && part.find_first_not_of("0123456789") == std::string::npos) {
        index = static_cast<std::size_t>(std::stoul(part));
      }
      if (index >= node->items.size()) break;
      if (last) {
        node->items.erase(node->items.begin() + static_cast<std::ptrdiff_t>(index));
        if (node->is_object()) node->names.erase(node->names.begin() + static_cast<std::ptrdiff_t>(index));
        dropped = true;
        break;
      }
      node = &node->items[index];
      start = dot + 1;
    }
    if (dropped) return true;
    const std::size_t cut = path.rfind('.');
    if (cut == std::string::npos) return false;
    path.erase(cut);
  }
  return false;
}
}  // namespace detail

// Loads the settings kept in flash, which an older (or newer) firmware may have written: a member this firmware does not know is ignored, one it cannot accept (out of range, too long, the wrong
// type, in conflict with another) is dropped so that the node uses its first value for that one thing alone - the rest of what was stored is kept, and the node never starts over because of one field.
// `dropped` lists what was left out. False only when the text is not JSON at all or a problem has no place in the document to blame; the caller then uses its first settings.
// `Settings` is the node's own type, with `read_settings(document, settings, problems)` and `validate(settings)` next to it in this namespace.
template <typename Settings, typename ProblemList>
bool load_stored(std::string_view text, const Settings& base, Settings& out, ProblemList& dropped) {
  dropped.clear();
  json::Value document;
  if (!json::parse(text, document)) { dropped.push_back({"", "not_json"}); return false; }
  for (int round = 0; round < 128; ++round) {
    // Settings is about 3 KB: it lives on the heap so that this reader, called with others of its size from the start-up of the main task, does not run that stack out.
    const auto candidate_box = std::make_unique<Settings>(base);
    Settings& candidate = *candidate_box;
    ProblemList problems;
    read_settings(document, candidate, problems);
    if (problems.empty()) problems = validate(candidate);
    if (problems.empty()) { out = std::move(candidate); return true; }
    const typename ProblemList::value_type culprit = problems.front();
    if (!detail::drop_path(document, culprit.path)) return false;
    dropped.push_back(culprit);
  }
  return false;
}


}  // namespace armor::config
