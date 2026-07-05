---
status: accepted
date: 2026-07-05
---

# Incremental reindex via changed-file reparse

On reindex, only files whose content changed are re-parsed; unchanged files reuse the local facts (ADR-0006's parse output) retained from the previous run, keyed by path and validated by content hash. A **full merge + resolve then runs over the combined facts** — the same Phases B–C a from-scratch index runs.

- **Detect** — crawl, read each file, and compare its fresh content hash to the hash the prior run recorded for that path.
- **Reparse** — parse only the changed and new files (the expensive tree-sitter parse + extraction); reuse the rest verbatim.
- **Rebuild** — feed the combined per-file facts through the unchanged merge + resolve.

Because resolve always reruns over every file's facts, a graph produced incrementally is **byte-for-byte identical** to a from-scratch index, and an edit that changes cross-file resolution (e.g. a `static` callee becoming external) is reflected correctly even in files that were reused untouched.

## Considered Options

- **Surgical in-place edge patching** — mutate the graph's nodes/edges for just the changed files. Rejected *for now*: cross-file resolution (linkage rules, ADR-0005) means one file's edit can change edges in files that did not change, so correct in-place patching is intricate. Kept as explicit future work.
- **Reparse-changed + full re-resolve (chosen)** — the parse is where the cost is, so skipping it for unchanged files captures most of the win while a full re-resolve keeps the graph provably identical to a cold index — correctness for free.

## Consequences

- The per-file parse facts must be retained between runs (returned from the index run and passed back in) — reuse needs the facts, not just the graph, since merge consumes facts.
- The win is measured as files reused vs. re-parsed; merge + resolve are cheap relative to parse, so re-running them fully is an acceptable cost for the identical-graph guarantee.
- Cross-process incremental (persisting facts to disk alongside the mmap'd graph of ADR-0008) is not built here; this slice delivers the in-process capability.
