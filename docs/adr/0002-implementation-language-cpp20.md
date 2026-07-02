---
status: accepted
date: 2026-07-02
---

# Implement the engine in C++20

We implement Cartograph in C++20 for direct control over the low-latency mechanisms that define the project: manual memory-layout control (arena / struct-of-arrays), `mmap`, and a thread pool. tree-sitter is a C library, so it links natively with zero FFI overhead.

## Considered Options

- **Go** — a faster route to a working tool, but a garbage collector plus `cgo` overhead for tree-sitter would work against the manual-memory-control and low-latency goals.
- **C++20 (chosen)** — slower to develop (manual memory management, CMake/tree-sitter build friction), accepted deliberately in exchange for full control over memory layout and concurrency (see ADR-0001).
