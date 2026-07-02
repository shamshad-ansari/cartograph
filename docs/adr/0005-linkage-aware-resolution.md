---
status: accepted
date: 2026-07-02
---

# Linkage-aware symbol resolution

Call sites are resolved to definitions using C's actual linkage rules over the globbed `.c`/`.h` file set: a `static` function resolves file-locally, otherwise to an `extern` definition, and header declarations link to their `.c` definitions (`#include` is modelled as edges). We deliberately do **not** run the preprocessor, expand macros, or read `compile_commands.json`.

This keeps setup to a single directory argument while still capturing real structure: it distinguishes two `static foo()`s in different files, and a definition from a call — distinctions a purely textual search cannot make.

## Considered Options

Resolution modes, from least to most precise:

- **Lexical** — match a callee to a definition by identifier text only. Rejected: this is what a text search already does, so it adds no structural information.
- **Linkage-aware (chosen)** — resolve via C linkage rules (`static`/`extern`, declaration → definition) over the indexed source set, with no build required. Captures genuine structure while staying simple and zero-setup.
- **Semantic** — resolve via the preprocessor and build metadata (`compile_commands.json`); type- and macro-accurate, but brittle and heavyweight. Deferred to future work; a future ADR would supersede this one.

## Consequences

- Macro-generated calls produce no `CALLS` edge; same-name, different-signature `extern` functions (a real-C link error) are linked-to-all-and-flagged. Both are documented limitations pinned by negative test fixtures.
