#pragma once

#include <filesystem>

#include "cartograph/graph.hpp"

namespace cartograph {

// Knobs for an indexing run.
struct IndexOptions {
  // Worker count for the parallel parse (Phase A) and resolve (Phase C) stages;
  // the merge (Phase B) is always sequential (ADR-0006). 0 means auto — the
  // hardware concurrency. 1 forces the single-threaded reference path. The
  // resulting graph is identical whatever the thread count.
  unsigned threads = 0;
};

// Recursively index every .c/.h file under `dir` into a code graph: function
// definitions, declarations, types, calls, includes, and type references, with
// linkage-aware resolution (ADR-0005). Files that cannot be read or fail to
// parse are skipped and recorded. Indexing runs as a three-phase share-nothing
// pipeline (ADR-0006) — parse, merge, resolve — with parse and resolve spread
// across `opts.threads` workers; the output does not depend on that count.
Graph index_directory(const std::filesystem::path& dir,
                      const IndexOptions& opts = {});

}  // namespace cartograph
