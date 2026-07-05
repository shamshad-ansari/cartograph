#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "cartograph/extractor.hpp"
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

// The retained result of parsing one file (Phase A): its self-contained local
// facts and the content hash they were derived from (ADR-0006). Returned from an
// index run and passed back into `reindex_directory` (issue 0016), which reuses
// the facts of any file whose hash is unchanged and re-parses only the rest.
struct ParsedFile {
  enum class Status { Ok, Unreadable, SyntaxError };

  std::filesystem::path path;
  Status status = Status::Ok;
  std::uint64_t hash = 0;
  std::vector<IncludeFact> includes;
  std::vector<DefinitionFact> definitions;
  std::vector<CallFact> calls;
  std::vector<DeclarationFact> declarations;
  std::vector<TypeFact> types;
  std::vector<TypeUseFact> type_uses;
};

// The outcome of an incremental-tracking index run: the graph, the per-file
// parse artifacts to feed the next reindex, and how many files this run
// re-parsed versus reused from the prior artifacts.
struct IndexResult {
  Graph graph;
  std::vector<ParsedFile> artifacts;  // one per indexed file, in crawl order
  std::size_t files_parsed = 0;       // files (re)parsed from source this run
  std::size_t files_reused = 0;       // files served from `prior` unchanged
};

// Recursively index every .c/.h file under `dir` into a code graph: function
// definitions, declarations, types, calls, includes, and type references, with
// linkage-aware resolution (ADR-0005). Files that cannot be read or fail to
// parse are skipped and recorded. Indexing runs as a three-phase share-nothing
// pipeline (ADR-0006) — parse, merge, resolve — with parse and resolve spread
// across `opts.threads` workers; the output does not depend on that count.
Graph index_directory(const std::filesystem::path& dir,
                      const IndexOptions& opts = {});

// Incrementally index `dir`, reusing the parse artifacts of any file whose
// content hash matches its entry in `prior` and re-parsing only changed or new
// files (issue 0016). The parse (Phase A) is the only stage that skips work; a
// full merge + resolve (Phases B–C) always runs over the combined facts, so
// cross-file resolution stays correct even when the edit was in another file.
// Pass an empty `prior` for a from-scratch index. The resulting graph is
// identical to what `index_directory` produces for the same tree.
IndexResult reindex_directory(const std::filesystem::path& dir,
                              const std::vector<ParsedFile>& prior,
                              const IndexOptions& opts = {});

}  // namespace cartograph
