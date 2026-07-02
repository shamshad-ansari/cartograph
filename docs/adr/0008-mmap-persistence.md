---
status: accepted
date: 2026-07-02
---

# Persist the index by memory-mapping the SoA layout

The on-disk index format **is** the raw in-memory struct-of-arrays layout plus a **string arena** (names stored as integer offset+length; no `std::string` inside the mapped region). A warm start therefore `mmap`s the file and uses the structures in place with near-zero parse cost ("cold index seconds → warm load milliseconds"). This depends on the position-independent, integer-ID layout of ADR-0007.

## Considered Options

- **Intermediate serialize/deserialize (e.g. JSON or a binary blob loaded into fresh objects)** — rejected: it still pays a load/allocation cost, and it neither teaches nor demonstrates the systems point, while adding a maintenance burden.
- **mmap of the flat layout (chosen)** — the primary persistence mechanism.

## Consequences

- v1 ships with **no persistence** (index in memory, query, exit); mmap is added only *after* the SoA migration, so its dependency (ADR-0007) is already in place.
