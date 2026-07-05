#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "cartograph/graph.hpp"

namespace cartograph {

// The evaluation harness (issue 0011): it quantifies cartograph's structural
// precision against a plain textual search over a human-labelled ground-truth
// set. grep/ripgrep is the baseline — the default code-navigation tool a coding
// agent reaches for today (ADR-0010) — not a competitor to beat, but the yardstick
// that turns "structural resolution is more precise" from a claim into a number.

// A source location in the coordinate space the metrics are computed in: a file
// key (a path relative to the corpus root, so ground truth is location-independent)
// and a 1-based line. Ordered and comparable so location sets can be intersected.
struct EvalLocation {
  std::string file;
  std::uint32_t line = 0;

  bool operator==(const EvalLocation& o) const {
    return line == o.line && file == o.file;
  }
  bool operator<(const EvalLocation& o) const {
    return file != o.file ? file < o.file : line < o.line;
  }
};

// The two navigation queries compared. Each fixes the coordinate space of its
// answers, so grep and cartograph are scored against the same ground truth:
//   - FindDefinition: the file:line of every function *definition* of the symbol.
//   - WhoCalls:       the file:line of every resolved *call site* of the symbol.
// Both are spaces grep can reach (it prints matching lines), which is what keeps
// the comparison fair — grep is scored on locations it can actually produce.
enum class EvalQuery {
  FindDefinition,
  WhoCalls,
};

// The stable string token for `query` used in the ground-truth file and reports
// ("find-definition", "who-calls"), and its inverse. `parse_eval_query` returns
// false for an unrecognized token.
std::string_view eval_query_name(EvalQuery query);
bool parse_eval_query(std::string_view token, EvalQuery& out);

// One human-labelled ground-truth entry: the true, verified answer set for a
// single query about a single symbol. `expected` may be empty — a legitimate
// answer (e.g. who-calls on a function nothing calls).
struct GroundTruth {
  EvalQuery query;
  std::string symbol;
  std::vector<EvalLocation> expected;
};

// Precision and recall of one retrieved set against one expected set, with the
// raw counts they are computed from so a report can show the work.
//   precision = |E ∩ R| / |R|   (1.0 when R is empty: nothing wrong was returned)
//   recall    = |E ∩ R| / |E|   (1.0 when E is empty: nothing was missed)
struct Score {
  std::size_t retrieved = 0;       // |R|
  std::size_t relevant = 0;        // |E|
  std::size_t true_positives = 0;  // |E ∩ R|
  double precision = 0;
  double recall = 0;
};

// Score `retrieved` against the ground-truth `expected`. Both are treated as
// sets: duplicate locations collapse, and order does not matter.
Score score(const std::vector<EvalLocation>& expected,
            const std::vector<EvalLocation>& retrieved);

// The grep/ripgrep baseline: every line under `dir` on which `symbol` appears as
// a whole identifier token, as file:line locations relative to `dir`. This is the
// in-process equivalent of `grep -rnw <symbol>` over the C sources — deterministic
// and free of an external binary — and, like grep, it is query-agnostic: it knows
// nothing of definitions or calls, only text. Results are sorted and deduplicated
// (one location per matching line, as `grep -n` prints).
std::vector<EvalLocation> grep_search(const std::filesystem::path& dir,
                                      const std::string& symbol);

// The cartograph answer for `query` about `symbol` over an already-built `graph`
// for `dir`, as file:line locations relative to `dir`. For FindDefinition this is
// the definition nodes; for WhoCalls it is the resolved call sites — the call-site
// lines whose enclosing function the graph actually linked to `symbol`, so C's
// linkage rules (static shadowing, unresolved names) are honoured, not guessed.
std::vector<EvalLocation> cartograph_query(const Graph& graph,
                                           const std::filesystem::path& dir,
                                           EvalQuery query,
                                           const std::string& symbol);

// One ground-truth entry scored for both tools — the per-query row of a report.
struct EvalRow {
  GroundTruth truth;
  Score grep;
  Score cartograph;
};

// Aggregate accuracy of one tool across every scored query: the mean (macro)
// precision and recall over the entries, which weights each query equally.
struct ToolAccuracy {
  std::string tool;
  std::size_t queries = 0;
  double precision = 0;  // mean precision over queries
  double recall = 0;     // mean recall over queries
};

// A full evaluation: the corpus, every scored query, and the two aggregates.
struct EvalReport {
  std::filesystem::path corpus;
  std::vector<EvalRow> rows;
  ToolAccuracy grep;
  ToolAccuracy cartograph;
};

// Index `dir` once, then run grep and cartograph for every ground-truth entry and
// score both. The graph is built a single time and shared across all queries.
EvalReport run_eval(const std::filesystem::path& dir,
                    const std::vector<GroundTruth>& truth);

// Load a ground-truth dataset from `file`. Each non-blank, non-`#` line is
//   <query> <symbol> [<file:line> ...]
// e.g. `who-calls list_push src/list.c:22 src/main.c:9`. A line with no locations
// is an entry whose true answer set is empty. Throws std::runtime_error on a
// malformed line (unknown query token or bad file:line).
std::vector<GroundTruth> load_ground_truth(const std::filesystem::path& file);

// Emit `report` as an aligned human-readable table: a per-query breakdown plus the
// grep-vs-cartograph aggregate, with the ADR-0010 framing note.
void write_eval_summary(const EvalReport& report, std::ostream& out);

// Emit `report` as JSON — the machine-readable form for tracking across changes.
void write_eval_json(const EvalReport& report, std::ostream& out);

}  // namespace cartograph
