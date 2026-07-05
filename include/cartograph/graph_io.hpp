#pragma once

#include <filesystem>

#include "cartograph/graph.hpp"

namespace cartograph {

// Persistence for the struct-of-arrays graph (ADR-0008, issue 0014). The on-disk
// format *is* the flat SoA layout plus the string arena, guarded by a magic and
// version header and addressed entirely by integer offsets — so it is position-
// independent and a warm start can `mmap` it and serve queries in place, paying
// no re-parse cost ("cold index seconds → warm load milliseconds").

// Write `graph` to a single file at `path`, creating or truncating it. The node
// columns and string arena are written as their raw in-memory bytes; the keyed
// indices (by-name, adjacency) are written as flat integer arrays the loader
// replays. Throws std::runtime_error if the file cannot be written.
void save_graph(const Graph& graph, const std::filesystem::path& path);

// Memory-map the index file at `path` and return a Graph that views the node
// columns and string arena directly in the mapping — no per-node copy, no re-
// parse — with its keyed indices rebuilt from the persisted arrays. The returned
// Graph owns the mapping and keeps it alive for its lifetime. Throws
// std::runtime_error if the file is missing, truncated, or carries a foreign
// magic or an incompatible version.
Graph load_graph(const std::filesystem::path& path);

}  // namespace cartograph
