---
status: accepted
date: 2026-07-02
---

# In-memory graph as integer-ID struct-of-arrays

The graph references nodes and edges by integer **NodeID** into flat arrays (moving toward struct-of-arrays + arena allocation), not by `Node*` pointers into a `map<string, vector<Node*>>`. Integer-ID SoA is cache-friendly for the traversal-heavy queries (`who_calls`, `blast_radius`) and, crucially, is *position-independent*, which is a prerequisite for the `mmap` persistence in ADR-0008: pointers cannot be memory-mapped across runs, integer offsets can.

## Consequences

- We build a **simple integer-ID `vector` graph first** for correctness and a baseline, then migrate to full SoA/arena as an explicit, before/after-benchmarked step so the performance impact is measured directly.
- Integer NodeIDs (never raw `Node*`) are used from day one, so the migration is a pure data-layout change rather than a rewrite.
