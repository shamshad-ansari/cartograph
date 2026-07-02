---
status: accepted
date: 2026-07-02
---

# Benchmark against grep, with manual ground truth

Cartograph is built on the premise that linkage-aware resolution (ADR-0005) recovers code structure a purely textual search cannot — resolving a call to the definition it actually binds to, rather than to every occurrence of the name. grep/ripgrep is the natural baseline: it is what coding agents use to navigate a codebase today, so a comparison against it quantifies the gain over the tool Cartograph would replace.

The benchmark therefore measures precision/recall against **grep/ripgrep** on a **manually-labelled ground-truth set** of ~20–30 symbols across C repositories (Redis, git, Linux kernel) — the evidence for or against the premise, rather than an assumption of it. Engine performance (index time, files/sec, LOC/sec, peak RSS, query-latency percentiles) is measured separately and automated.

## Considered Options

- **cscope / GNU GLOBAL as the baseline** — rejected as the *headline* comparison. These are mature, specialized C indexers, but they serve editor-driven human lookup rather than the agent workflow Cartograph targets, so "faster than cscope" is not the claim being made. Cartograph's distinct contribution is structural precision, a graph API, and MCP integration for agents.
- **Clang-based ground-truth oracle** — rejected: it would pull in the exact compiler frontend ADR-0004 chose not to depend on. Manual labelling stays honest and controllable.

## Consequences

- The agent-in-the-loop navigation benchmark (measuring an agent's steps/time) needs a separate agent harness and is deferred to future work.
