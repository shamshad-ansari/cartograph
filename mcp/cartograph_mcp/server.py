"""MCP adapter exposing the cartograph engine as MCP tools (ADR-0009).

A thin FastMCP server that forwards `find_definition`, `who_calls`, and
`blast_radius` tool calls to the CLI engine and returns structured results. All
protocol plumbing lives in the official MCP SDK; none of it lives in the C++
core.
"""

from __future__ import annotations

from mcp.server.fastmcp import FastMCP
from pydantic import BaseModel

from .engine import Engine


class Match(BaseModel):
    """One query hit: a source file and its 1-based line number."""

    file: str
    line: int


class QueryResult(BaseModel):
    """Structured tool output: the ordered list of matching sites."""

    matches: list[Match]


def build_server(engine: Engine | None = None) -> FastMCP:
    """Build the MCP server, optionally against a specific engine.

    Tests inject an `Engine` pointed at the freshly built binary; the default
    resolves `cartograph` from the environment.
    """
    engine = engine or Engine()
    mcp = FastMCP("cartograph")

    def _result(matches) -> QueryResult:
        return QueryResult(matches=[Match(**m.as_dict()) for m in matches])

    @mcp.tool()
    def find_definition(name: str, directory: str) -> QueryResult:
        """Locate every definition of function `name` under `directory`."""
        return _result(engine.find_definition(name, directory))

    @mcp.tool()
    def who_calls(name: str, directory: str) -> QueryResult:
        """List every function that directly calls `name` under `directory`."""
        return _result(engine.who_calls(name, directory))

    @mcp.tool()
    def blast_radius(name: str, directory: str) -> QueryResult:
        """List every direct and indirect caller of `name` under `directory`."""
        return _result(engine.blast_radius(name, directory))

    return mcp


def main() -> None:
    """Run the adapter over stdio for an MCP client to launch."""
    build_server().run()


if __name__ == "__main__":
    main()
