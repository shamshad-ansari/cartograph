---
status: accepted
date: 2026-07-02
---

# Index C source code, not C++ or Python

Cartograph resolves a call site to the *specific* definition it binds to — following `static`/`extern` linkage rather than matching a name textually. That resolution is only well-defined when the language's linkage rules are simple enough to model faithfully, which is why the target is **C**, even though Cartograph is itself written in C++. C's small grammar also keeps tree-sitter parsing reliable.

## Considered Options

- **C++ as target** — rejected for v1: templates, overloads, and macros turn structural call resolution into a near-research problem that would consume the schedule on parser edge cases rather than the core engine.
- **Python as target** — rejected: dynamic dispatch reduces call resolution to name-matching, so the index would carry no more structural information than a plain text search.
- **C (chosen)** — easy to parse, and its linkage rules make call → definition resolution well-defined.

## Consequences

- All C++ OO node/edge types (Class, Method, Namespace, INHERITS, OVERRIDES, templates) are dropped from the data model; full C++ support is future work.
- Evaluation repositories are C projects (Redis, git, Linux kernel), not the C++ repos (LLVM, fmt, spdlog) in the original plan.
