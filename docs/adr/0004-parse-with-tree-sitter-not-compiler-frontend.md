---
status: accepted
date: 2026-07-02
---

# Parse with tree-sitter, not a compiler frontend

We extract structure with **tree-sitter-c**, not a real compiler frontend (libclang / clangd). tree-sitter is lightweight and error-tolerant and needs no build flags or `compile_commands.json`, so the tool works by simply pointing at a directory — the developer experience we want (see ADR-0001). The cost is that we see *syntax*, not full *semantics*.

## Considered Options

- **libclang / clangd** — gives type-accurate resolution and macro expansion, but requires a working build environment per project and heavyweight integration, contradicting the "point at a folder" goal.
- **tree-sitter-c (chosen)** — syntactic only, but zero-setup, fast, and easy to extend to other languages.

## Consequences

- Resolution is linkage-based, not type-based (see ADR-0005); macros are not expanded.
- Extraction is expressed as tree-sitter `.scm` query patterns and kept **separate** from resolution logic, so supporting a new language is mostly a matter of writing new query files.
