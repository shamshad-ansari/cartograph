# Precision & recall: structural resolution vs textual search

Cartograph's premise is that linkage-aware resolution (ADR-0005) recovers code
structure a purely textual search cannot — binding a call to the definition it
*actually* resolves to, rather than to every occurrence of the name. This is where
that premise is measured rather than asserted.

## The baseline: grep

grep/ripgrep is the yardstick because it is the default code-navigation tool a
coding agent reaches for today. The comparison is not "beat grep" — grep is fast,
ubiquitous, and the right tool for a text search. It is the reference point that
turns "structural resolution is more precise" into a number, and it quantifies the
gain over the tool Cartograph augments.

The specialized incumbents — **cscope** and **GNU GLOBAL** — are mature C indexers,
acknowledged but deliberately not the headline baseline (ADR-0010): they serve
editor-driven human lookup, whereas Cartograph targets the agent workflow, and its
distinct contribution is *structural precision, a graph API, and MCP integration*.

## Method

The harness (`cartograph eval`) scores both tools against a **human-labelled,
human-verified ground-truth set** (`tests/fixtures/eval/ground-truth.txt`) over a
checked-in C corpus. Each entry is one query about one symbol:

- **find-definition** — the true answer is every definition *site* of the symbol.
- **who-calls** — the true answer is every source line that *actually calls* it.

Both queries are scored in the same coordinate space grep can reach — file:line
source locations — so grep is measured on locations it can genuinely produce, not
penalised for a coordinate mismatch. For each entry:

- **precision** = |correct ∩ retrieved| / |retrieved| — how much of what a tool
  returned was a real answer.
- **recall** = |correct ∩ retrieved| / |correct| — how much of the real answer a
  tool found.

grep is query-agnostic: it matches the symbol as a whole token (`grep -rnw`) and
returns every line it appears on. Cartograph answers structurally — definition
nodes for find-definition; for who-calls, the resolved call sites, where the
resolution verdict is the graph's own (static shadowing and unresolved names are
honoured, never re-guessed by the harness).

The corpus is seeded with exactly the cases textual search cannot distinguish from
a real hit: header declarations, a function name inside a comment, a name inside a
string literal (`"store_open failed"`), and two file-local `static` functions both
named `init`.

## Results

On the labelled corpus (22 queries):

| tool       | precision | recall |
|------------|-----------|--------|
| grep       | 0.36      | 1.00   |
| cartograph | 1.00      | 1.00   |

grep recovers every true answer (recall 1.00) — a real call or definition always
contains the name — but pays for it in precision: definitions, declarations,
comments, and string literals it cannot tell apart from real hits drag it to 0.36.
Cartograph returns exactly the true set. The sharpest case is `who-calls init`:
two real call sites, one per file-local `static init`; grep also returns both
definitions and both comment mentions (2 of 6 relevant), while Cartograph resolves
each call to its own translation unit's `init`.

## Running it

```
cartograph eval <corpus-dir> --truth <ground-truth-file> [--json]
```

`--json` emits the machine-readable form for tracking across changes; the default
is the aligned summary table. The checked-in corpus is reproducible and needs no
grep binary (the baseline is computed in-process with `grep -rnw` semantics). The
same harness runs against a real repository (e.g. Redis, git) by pointing `--dir`
and `--truth` at a ground-truth set labelled over it — the next step in turning the
fixture-scale result into evidence at repository scale.
