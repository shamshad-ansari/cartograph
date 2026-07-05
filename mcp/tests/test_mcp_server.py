"""End-to-end test of the MCP adapter (issue 0015).

Drives the adapter the way a real MCP client would — a `tools/list` followed by
`tools/call` over an in-memory session — and asserts the structured results the
engine returns for a fixture repo. The adapter forwards to the actual
`cartograph` binary, so this exercises the whole shim -> CLI -> engine path.
"""

from pathlib import Path

import pytest
from mcp.shared.memory import (
    create_connected_server_and_client_session as connect_session,
)

from cartograph_mcp.engine import Engine
from cartograph_mcp.server import build_server

REPO_ROOT = Path(__file__).resolve().parents[2]
BINARY = REPO_ROOT / "build" / "cartograph"
FIXTURE = Path(__file__).parent / "fixtures" / "chain"
CHAIN_C = str(FIXTURE / "chain.c")


def make_server():
    """A server wired to the built binary and forwarding to the real engine."""
    return build_server(Engine(binary=str(BINARY)))


async def call(client, tool, name):
    result = await client.call_tool(
        tool, {"name": name, "directory": str(FIXTURE)}
    )
    assert result.isError is False
    return result.structuredContent["matches"]


async def test_tools_list_exposes_the_three_queries():
    async with connect_session(make_server()) as client:
        listed = await client.list_tools()
        names = {tool.name for tool in listed.tools}
    assert {"find_definition", "who_calls", "blast_radius"} <= names


async def test_find_definition_returns_the_definition_site():
    async with connect_session(make_server()) as client:
        matches = await call(client, "find_definition", "leaf")
    assert matches == [{"file": CHAIN_C, "line": 3}]


async def test_who_calls_returns_direct_callers():
    async with connect_session(make_server()) as client:
        matches = await call(client, "who_calls", "leaf")
    # mid() calls leaf() directly; top() does not.
    assert matches == [{"file": CHAIN_C, "line": 7}]


async def test_blast_radius_returns_transitive_callers():
    async with connect_session(make_server()) as client:
        matches = await call(client, "blast_radius", "leaf")
    # mid() (direct) and top() (two hops out) are both in the blast radius.
    assert matches == [
        {"file": CHAIN_C, "line": 7},
        {"file": CHAIN_C, "line": 11},
    ]
