---
status: accepted
date: 2026-07-02
---

# Three-phase, share-nothing indexing pipeline

Indexing is structured as **parse → merge → resolve**:

- **Parse** — each file is parsed independently and emits self-contained local facts (definitions + unresolved call-site names/locations) with *no* access to the shared graph.
- **Merge** — a single thread stitches facts together, assigns integer NodeIDs, and builds the global symbol table (applying the ADR-0005 linkage rules).
- **Resolve** — call sites are resolved against the now-immutable symbol table.

This shape makes the two expensive phases — parse and resolve — embarrassingly parallel with **zero lock contention**, so concurrency is added later by swapping their per-file loops for a thread pool, not by locking a shared graph.

## Considered Options

- **Shared mutable graph with fine-grained locking** — rejected: lock contention and concurrency bugs, with no structural guarantee that contention is avoided.
- **Share-nothing map-reduce shape (chosen)** — contention is avoided structurally.

## Consequences

- Parse output must carry no NodeIDs (names/locations only) so workers never touch shared state.
- Built single-threaded first; the thread pool is added as a measured optimization (speedup + thread-scaling curve).
