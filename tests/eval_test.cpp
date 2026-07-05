#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/eval.hpp"
#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

using cartograph::EvalLocation;

// Concise EvalLocation set literal for the score() tests.
std::vector<EvalLocation> locs(std::initializer_list<EvalLocation> in) {
  return std::vector<EvalLocation>(in);
}

std::filesystem::path calls_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "calls";
}

// A location list sorted into canonical set order, so tests can compare against
// an expected set without caring about the order the harness produced.
std::vector<EvalLocation> sorted(std::vector<EvalLocation> in) {
  std::sort(in.begin(), in.end());
  return in;
}

std::vector<EvalLocation> grep(const std::string& symbol) {
  return sorted(cartograph::grep_search(calls_dir(), symbol));
}

std::vector<EvalLocation> query(cartograph::EvalQuery q, const std::string& symbol) {
  const cartograph::Graph graph = cartograph::index_directory(calls_dir());
  return sorted(cartograph::cartograph_query(graph, calls_dir(), q, symbol));
}

}  // namespace

// A perfect answer — retrieved exactly equals expected — scores 1.0 precision and
// 1.0 recall, and the counts line up. This is the cartograph ideal on the corpus.
TEST(Score, PerfectAnswerIsOneOne) {
  const auto expected = locs({{"a.c", 1}, {"a.c", 2}, {"b.c", 3}});
  const cartograph::Score s = cartograph::score(expected, expected);

  EXPECT_EQ(s.relevant, 3u);
  EXPECT_EQ(s.retrieved, 3u);
  EXPECT_EQ(s.true_positives, 3u);
  EXPECT_DOUBLE_EQ(s.precision, 1.0);
  EXPECT_DOUBLE_EQ(s.recall, 1.0);
}

// The grep pattern: it returns every mention, so it recovers all the true hits
// (recall 1.0) but drowns them in false positives (precision < 1.0). Here 1 of 3
// retrieved is relevant.
TEST(Score, SupersetHasFullRecallLowPrecision) {
  const auto expected = locs({{"a.c", 5}});
  const auto retrieved = locs({{"a.c", 1}, {"a.c", 5}, {"a.c", 9}});
  const cartograph::Score s = cartograph::score(expected, retrieved);

  EXPECT_EQ(s.true_positives, 1u);
  EXPECT_DOUBLE_EQ(s.recall, 1.0);
  EXPECT_DOUBLE_EQ(s.precision, 1.0 / 3.0);
}

// Locations are treated as sets: a duplicate in either list does not inflate any
// count. grep prints one line per match, but the harness is robust regardless.
TEST(Score, DeduplicatesLocations) {
  const auto expected = locs({{"a.c", 5}, {"a.c", 5}});
  const auto retrieved = locs({{"a.c", 5}, {"a.c", 5}, {"a.c", 5}});
  const cartograph::Score s = cartograph::score(expected, retrieved);

  EXPECT_EQ(s.relevant, 1u);
  EXPECT_EQ(s.retrieved, 1u);
  EXPECT_EQ(s.true_positives, 1u);
  EXPECT_DOUBLE_EQ(s.precision, 1.0);
  EXPECT_DOUBLE_EQ(s.recall, 1.0);
}

// Empty-set conventions: returning nothing is vacuously precise (no wrong answer),
// and an empty truth set is vacuously fully recalled (nothing to miss). This keeps
// entries like "who-calls <uncalled function>" from poisoning the aggregate.
TEST(Score, EmptySetsAreVacuouslyOne) {
  const cartograph::Score empty_retrieved = cartograph::score(locs({{"a.c", 1}}), {});
  EXPECT_DOUBLE_EQ(empty_retrieved.precision, 1.0);
  EXPECT_DOUBLE_EQ(empty_retrieved.recall, 0.0);

  const cartograph::Score empty_expected = cartograph::score({}, locs({{"a.c", 1}}));
  EXPECT_DOUBLE_EQ(empty_expected.precision, 0.0);
  EXPECT_DOUBLE_EQ(empty_expected.recall, 1.0);

  const cartograph::Score both_empty = cartograph::score({}, {});
  EXPECT_DOUBLE_EQ(both_empty.precision, 1.0);
  EXPECT_DOUBLE_EQ(both_empty.recall, 1.0);
}

// ── grep_search: the textual baseline ──────────────────────────────────────
using cartograph::EvalQuery;

// grep matches the symbol as a whole token on every line it appears — the
// definition, every call site, nothing more, nothing less about *what* the line
// is. For `helper` that is its definition (app.c:1) and its three call sites.
TEST(GrepSearch, MatchesEveryWholeWordOccurrence) {
  EXPECT_EQ(grep("helper"),
            (std::vector<EvalLocation>{
                {"app.c", 1}, {"app.c", 6}, {"app.c", 14}, {"lib.c", 2}}));
}

// A whole-word match, like `grep -w`: it lists both definitions of `shared` and
// the one call, across files — text, with no notion of which is which.
TEST(GrepSearch, IsWholeWordAcrossFiles) {
  EXPECT_EQ(grep("shared"),
            (std::vector<EvalLocation>{
                {"dup1.c", 1}, {"dup1.c", 6}, {"dup2.c", 1}}));
}

// ── cartograph_query: find-definition ──────────────────────────────────────

// find-definition returns exactly the definition sites — one for a normal
// symbol, and both for a name with two external definitions (a link error the
// index surfaces rather than hides).
TEST(CartographQuery, FindDefinitionReturnsDefinitionsOnly) {
  EXPECT_EQ(query(EvalQuery::FindDefinition, "helper"),
            (std::vector<EvalLocation>{{"app.c", 1}}));
  EXPECT_EQ(query(EvalQuery::FindDefinition, "shared"),
            (std::vector<EvalLocation>{{"dup1.c", 1}, {"dup2.c", 1}}));
}

// ── cartograph_query: who-calls ────────────────────────────────────────────

// who-calls returns the *call sites* (not the definition, not mentions) — the
// coordinate space grep is scored against. For `helper`: the three lines that
// actually invoke it, and never its definition line.
TEST(CartographQuery, WhoCallsReturnsResolvedCallSites) {
  EXPECT_EQ(query(EvalQuery::WhoCalls, "helper"),
            (std::vector<EvalLocation>{
                {"app.c", 6}, {"app.c", 14}, {"lib.c", 2}}));
  EXPECT_EQ(query(EvalQuery::WhoCalls, "compute"),
            (std::vector<EvalLocation>{{"app.c", 8}}));
}

// Linkage resolution is honoured: the call in caller_one binds to `shared`, so
// its call site is reported — but neither `shared` *definition* line is, even
// though grep would surface both. This is the precision the harness quantifies.
TEST(CartographQuery, WhoCallsResolvesPastDefinitions) {
  EXPECT_EQ(query(EvalQuery::WhoCalls, "shared"),
            (std::vector<EvalLocation>{{"dup1.c", 6}}));
}

// A function nothing calls has an empty resolved call-site set — a legitimate,
// vacuously-perfect answer rather than a match on its own definition line.
TEST(CartographQuery, WhoCallsUncalledIsEmpty) {
  EXPECT_TRUE(query(EvalQuery::WhoCalls, "main").empty());
}

// ── load_ground_truth + run_eval over the labelled corpus ───────────────────
namespace {

std::filesystem::path eval_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "eval";
}

std::filesystem::path truth_file() { return eval_dir() / "ground-truth.txt"; }

const cartograph::EvalRow* find_row(const cartograph::EvalReport& r,
                                    cartograph::EvalQuery q,
                                    const std::string& symbol) {
  for (const cartograph::EvalRow& row : r.rows) {
    if (row.truth.query == q && row.truth.symbol == symbol) return &row;
  }
  return nullptr;
}

}  // namespace

// The dataset parses: comments and blanks are skipped, every entry is read, and a
// multi-location label and a deliberately-empty one both round-trip.
TEST(LoadGroundTruth, ParsesLabelledDataset) {
  const std::vector<cartograph::GroundTruth> truth =
      cartograph::load_ground_truth(truth_file());
  EXPECT_EQ(truth.size(), 22u);

  const auto* init_defs = [&]() -> const cartograph::GroundTruth* {
    for (const auto& g : truth) {
      if (g.query == EvalQuery::FindDefinition && g.symbol == "init") return &g;
    }
    return nullptr;
  }();
  ASSERT_NE(init_defs, nullptr);
  EXPECT_EQ(sorted(init_defs->expected),
            (std::vector<EvalLocation>{{"cache.c", 4}, {"util.c", 2}}));

  const auto* uncalled = [&]() -> const cartograph::GroundTruth* {
    for (const auto& g : truth) {
      if (g.query == EvalQuery::WhoCalls && g.symbol == "cache_insert") return &g;
    }
    return nullptr;
  }();
  ASSERT_NE(uncalled, nullptr);
  EXPECT_TRUE(uncalled->expected.empty());
}

// A malformed line is a labelling error worth surfacing loudly, not swallowing.
TEST(LoadGroundTruth, ThrowsOnMalformedLine) {
  const std::filesystem::path bad =
      std::filesystem::temp_directory_path() / "cartograph_bad_truth.txt";
  std::ofstream(bad) << "who-calls foo not_a_location\n";
  EXPECT_THROW(cartograph::load_ground_truth(bad), std::runtime_error);
  std::filesystem::remove(bad);
}

// The headline result: over the human-labelled set, cartograph is perfectly
// precise and complete, while grep — which recovers every true hit (recall 1.0) —
// pays for it in precision, dragged down by definitions, declarations, comments,
// and string literals it cannot tell from real answers.
TEST(RunEval, CartographIsPreciseGrepIsNot) {
  const cartograph::EvalReport report =
      cartograph::run_eval(eval_dir(), cartograph::load_ground_truth(truth_file()));

  EXPECT_EQ(report.rows.size(), 22u);
  EXPECT_EQ(report.cartograph.queries, 22u);
  EXPECT_EQ(report.grep.queries, 22u);

  EXPECT_DOUBLE_EQ(report.cartograph.precision, 1.0);
  EXPECT_DOUBLE_EQ(report.cartograph.recall, 1.0);
  EXPECT_DOUBLE_EQ(report.grep.recall, 1.0);
  EXPECT_LT(report.grep.precision, 1.0);
}

// A concrete row shows the mechanism: `who-calls init` has two true call sites,
// one per file-local `static init`. grep also returns each definition and the two
// comment mentions (6 hits, 2 relevant); cartograph returns exactly the two calls.
TEST(RunEval, WhoCallsInitRowQuantifiesTheGap) {
  const cartograph::EvalReport report =
      cartograph::run_eval(eval_dir(), cartograph::load_ground_truth(truth_file()));

  const cartograph::EvalRow* row =
      find_row(report, EvalQuery::WhoCalls, "init");
  ASSERT_NE(row, nullptr);

  EXPECT_EQ(row->cartograph.true_positives, 2u);
  EXPECT_EQ(row->cartograph.retrieved, 2u);
  EXPECT_DOUBLE_EQ(row->cartograph.precision, 1.0);
  EXPECT_DOUBLE_EQ(row->cartograph.recall, 1.0);

  EXPECT_EQ(row->grep.true_positives, 2u);
  EXPECT_EQ(row->grep.retrieved, 6u);
  EXPECT_DOUBLE_EQ(row->grep.recall, 1.0);
  EXPECT_DOUBLE_EQ(row->grep.precision, 2.0 / 6.0);
}

// ── report rendering ────────────────────────────────────────────────────────
namespace {

cartograph::EvalReport corpus_report() {
  return cartograph::run_eval(eval_dir(),
                              cartograph::load_ground_truth(truth_file()));
}

}  // namespace

// The human summary carries the table (both tools, precision and recall), names a
// sampled symbol, and states the ADR-0010 framing: grep as the default navigation
// tool, with cscope/GNU GLOBAL acknowledged as the specialized incumbents.
TEST(WriteEvalSummary, RendersTableAndFraming) {
  std::ostringstream out;
  cartograph::write_eval_summary(corpus_report(), out);
  const std::string s = out.str();

  EXPECT_NE(s.find("grep"), std::string::npos);
  EXPECT_NE(s.find("cartograph"), std::string::npos);
  EXPECT_NE(s.find("precision"), std::string::npos);
  EXPECT_NE(s.find("recall"), std::string::npos);
  EXPECT_NE(s.find("store_open"), std::string::npos);
  EXPECT_NE(s.find("cscope"), std::string::npos);
  EXPECT_NE(s.find("GNU GLOBAL"), std::string::npos);
}

// JSON output carries the machine-readable keys a tracking pipeline reads: the
// per-tool aggregate and the per-query rows.
TEST(WriteEvalJson, EmitsAggregateAndRows) {
  std::ostringstream out;
  cartograph::write_eval_json(corpus_report(), out);
  const std::string j = out.str();

  EXPECT_NE(j.find("\"aggregate\""), std::string::npos);
  EXPECT_NE(j.find("\"grep\""), std::string::npos);
  EXPECT_NE(j.find("\"cartograph\""), std::string::npos);
  EXPECT_NE(j.find("\"precision\""), std::string::npos);
  EXPECT_NE(j.find("\"recall\""), std::string::npos);
  EXPECT_NE(j.find("\"rows\""), std::string::npos);
  EXPECT_NE(j.find("who-calls"), std::string::npos);
}
