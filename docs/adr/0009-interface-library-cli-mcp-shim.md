---
status: accepted
date: 2026-07-02
---

# Library + CLI core; MCP via a thin external Python shim

The engine is a C++ library (`libcartograph`) wrapped by a CLI (`cartograph`, alias `carto`). MCP support is a **thin Python adapter** (official MCP SDK) that forwards tool calls to the engine — *not* MCP JSON-RPC hand-rolled in C++.

The CLI is the critical path: it is what the tests, the grep benchmark, and the demo all run through, and it needs no protocol. MCP's JSON-RPC/stdio plumbing is protocol boilerplate unrelated to the engine's core concerns and is more cumbersome to implement in C++ than in Python, so it lives outside the C++ core.

## Consequences

- An optional Unix-socket server mode is added only if per-query process-startup cost shows up in the benchmark numbers; the default is spawn-per-query via the CLI.
