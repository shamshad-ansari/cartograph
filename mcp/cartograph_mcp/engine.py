"""Subprocess bridge to the cartograph CLI engine (ADR-0009).

No MCP protocol logic lives here. The adapter spawns the `cartograph` binary
once per query — the default execution model from ADR-0009 — and parses its
`file:line` stdout into structured `Match` records. The binary is resolved from
the ``CARTOGRAPH_BIN`` environment variable, falling back to ``cartograph`` on
``PATH``.
"""

from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass


DEFAULT_BINARY = os.environ.get("CARTOGRAPH_BIN", "cartograph")


@dataclass(frozen=True)
class Match:
    """A single query hit: a source file and the 1-based line within it."""

    file: str
    line: int

    def as_dict(self) -> dict:
        return {"file": self.file, "line": self.line}


class EngineError(RuntimeError):
    """Raised when the underlying `cartograph` invocation fails."""


class Engine:
    """Forwards graph queries to the `cartograph` CLI and structures the output.

    Each method maps to one CLI subcommand and passes the target directory
    through unchanged, so the engine indexes (or warm-starts from a persisted
    index) exactly the tree the caller names.
    """

    def __init__(self, binary: str = DEFAULT_BINARY):
        self.binary = binary

    def find_definition(self, name: str, directory: str) -> list[Match]:
        return self._run("find-definition", name, directory)

    def who_calls(self, name: str, directory: str) -> list[Match]:
        return self._run("who-calls", name, directory)

    def blast_radius(self, name: str, directory: str) -> list[Match]:
        return self._run("blast-radius", name, directory)

    def _run(self, command: str, name: str, directory: str) -> list[Match]:
        proc = subprocess.run(
            [self.binary, command, name, "--dir", str(directory)],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            detail = proc.stderr.strip() or f"exit code {proc.returncode}"
            raise EngineError(f"cartograph {command} failed: {detail}")
        return _parse_matches(proc.stdout)


def _parse_matches(stdout: str) -> list[Match]:
    """Turn the CLI's ``path:line`` lines into `Match` records.

    Splits from the right so paths containing a colon survive; blank lines
    (empty result sets) yield an empty list.
    """
    matches: list[Match] = []
    for raw in stdout.splitlines():
        line = raw.strip()
        if not line:
            continue
        path, _, lineno = line.rpartition(":")
        matches.append(Match(file=path, line=int(lineno)))
    return matches
