---
status: accepted
date: 2026-07-02
---

# Prioritize a clean, well-documented systems design over exhaustive coverage

Cartograph exists to give developers and coding agents a fast, structural map of a C codebase - resolving how code actually connects (calls, definitions, dependencies) rather than matching text. To serve that purpose well, we optimize for a clean, well-understood systems design with measurable performance, and treat broad edge-case coverage as secondary. When edge-case correctness conflicts with a simple, maintainable design, we favor the simpler design and record the gap as an explicit, tested limitation rather than chasing it.

This is the decision framework behind the deliberate scope boundaries elsewhere (e.g. ADR-0005 on macros / `compile_commands.json`, ADR-0008 on deferred persistence): those are intentional choices, not oversights.

## Consequences

- A running "current limitations" and "future improvements" record is maintained incrementally, and limitations are pinned by negative test fixtures.
- The architecture keeps clear seams where a production-grade version would extend or diverge, so the project can evolve into a full production tool.
