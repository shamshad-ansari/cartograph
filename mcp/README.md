# Cartograph MCP adapter

A thin Python shim that exposes the cartograph engine to MCP-compatible clients
(Claude Desktop, Claude Code, any MCP host). It speaks the [Model Context
Protocol](https://modelcontextprotocol.io) using the official Python SDK and
forwards each tool call to the `cartograph` CLI — **the MCP protocol is not
implemented in the C++ core** (see [ADR-0009](../docs/adr/0009-interface-library-cli-mcp-shim.md)).

## Tools

Each tool takes a function `name` and a target `directory`, and returns
structured `{ "matches": [ { "file", "line" }, ... ] }` results.

| Tool              | Answers                                              |
| ----------------- | ---------------------------------------------------- |
| `find_definition` | Where is `name` defined?                             |
| `who_calls`       | Which functions call `name` directly?                |
| `blast_radius`    | Every direct and indirect caller of `name`.          |

## Setup

Build the engine first, so the `cartograph` binary exists:

```sh
cmake -S . -B build && cmake --build build   # from the repo root
```

Then install the adapter into a virtualenv:

```sh
cd mcp
python3 -m venv .venv
.venv/bin/pip install -e .
```

The adapter locates the engine via the `CARTOGRAPH_BIN` environment variable,
falling back to `cartograph` on `PATH`. Point it at the freshly built binary:

```sh
export CARTOGRAPH_BIN="$(pwd)/../build/cartograph"
```

## Running

The adapter serves over stdio, which is how MCP hosts launch it:

```sh
.venv/bin/cartograph-mcp
```

Register it with an MCP client, e.g. in a Claude Desktop config:

```json
{
  "mcpServers": {
    "cartograph": {
      "command": "/absolute/path/to/mcp/.venv/bin/cartograph-mcp",
      "env": { "CARTOGRAPH_BIN": "/absolute/path/to/build/cartograph" }
    }
  }
}
```

## Tests

The test suite drives the adapter as a real client would — a `tools/list` and
`tools/call` over an in-memory MCP session against a fixture repo — and asserts
the structured results:

```sh
.venv/bin/pip install -e ".[test]"
.venv/bin/python -m pytest tests
```
