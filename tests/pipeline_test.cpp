#include <atomic>
#include <cstddef>
#include <filesystem>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"
#include "cartograph/parallel.hpp"

namespace {

std::filesystem::path crawl_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "crawl";
}

// Whole-graph structural equality, addressed by NodeId. The parallel pipeline
// must reproduce the single-threaded graph bit for bit — same node ids in the
// same order, same edges in the same insertion order, same side tables — so a
// field-by-field comparison over every id is the regression oracle.
void expect_graphs_identical(const cartograph::Graph& a,
                             const cartograph::Graph& b) {
  ASSERT_EQ(a.size(), b.size());
  ASSERT_EQ(a.edge_count(), b.edge_count());

  for (cartograph::NodeId id = 0; id < a.size(); ++id) {
    const cartograph::NodeView na = a.node(id);
    const cartograph::NodeView nb = b.node(id);
    EXPECT_EQ(na.kind, nb.kind) << "node " << id;
    EXPECT_EQ(na.name, nb.name) << "node " << id;
    EXPECT_EQ(na.file, nb.file) << "node " << id;
    EXPECT_EQ(na.line, nb.line) << "node " << id;
    EXPECT_EQ(na.linkage, nb.linkage) << "node " << id;
    EXPECT_EQ(na.hash, nb.hash) << "node " << id;

    EXPECT_EQ(a.callers_of(id), b.callers_of(id)) << "callers of " << id;
    EXPECT_EQ(a.includes_of(id), b.includes_of(id)) << "includes of " << id;
    EXPECT_EQ(a.included_by(id), b.included_by(id)) << "included_by " << id;
    EXPECT_EQ(a.users_of(id), b.users_of(id)) << "users of " << id;
    EXPECT_EQ(a.definition_of(id), b.definition_of(id)) << "definition of " << id;
  }

  ASSERT_EQ(a.diagnostics().size(), b.diagnostics().size());
  for (std::size_t i = 0; i < a.diagnostics().size(); ++i) {
    const cartograph::Diagnostic& da = a.diagnostics()[i];
    const cartograph::Diagnostic& db = b.diagnostics()[i];
    EXPECT_EQ(da.callee, db.callee);
    EXPECT_EQ(da.caller_file, db.caller_file);
    EXPECT_EQ(da.caller_line, db.caller_line);
    EXPECT_EQ(da.candidates, db.candidates);
  }

  ASSERT_EQ(a.unresolved_includes().size(), b.unresolved_includes().size());
  for (std::size_t i = 0; i < a.unresolved_includes().size(); ++i) {
    EXPECT_EQ(a.unresolved_includes()[i].target,
              b.unresolved_includes()[i].target);
    EXPECT_EQ(a.unresolved_includes()[i].includer,
              b.unresolved_includes()[i].includer);
  }

  ASSERT_EQ(a.skipped_files().size(), b.skipped_files().size());
  for (std::size_t i = 0; i < a.skipped_files().size(); ++i) {
    EXPECT_EQ(a.skipped_files()[i].path, b.skipped_files()[i].path);
    EXPECT_EQ(a.skipped_files()[i].reason, b.skipped_files()[i].reason);
  }
}

}  // namespace

// parallel_for is a work-queue pool: it must invoke body exactly once for every
// index in [0, count), whatever the worker count.
TEST(ParallelFor, VisitsEveryIndexExactlyOnce) {
  constexpr std::size_t kCount = 10000;
  for (const unsigned threads : {1u, 2u, 4u, 8u}) {
    std::vector<int> seen(kCount, 0);
    cartograph::parallel_for(kCount, threads, [&](std::size_t i) {
      seen[i] += 1;  // disjoint index per call: no synchronization needed
    });
    const long total = std::accumulate(seen.begin(), seen.end(), 0L);
    EXPECT_EQ(total, static_cast<long>(kCount)) << "threads=" << threads;
    for (std::size_t i = 0; i < kCount; ++i) {
      ASSERT_EQ(seen[i], 1) << "index " << i << " threads=" << threads;
    }
  }
}

// An empty range is a no-op — it must not spawn work or touch the body.
TEST(ParallelFor, EmptyRangeIsNoop) {
  std::atomic<int> calls{0};
  cartograph::parallel_for(0, 8, [&](std::size_t) { calls.fetch_add(1); });
  EXPECT_EQ(calls.load(), 0);
}

// The regression oracle: parallel indexing yields a graph identical to the
// single-threaded one on the same input, for every thread count.
TEST(Pipeline, ParallelGraphMatchesSingleThreaded) {
  const cartograph::Graph baseline =
      cartograph::index_directory(crawl_dir(), cartograph::IndexOptions{1});

  for (const unsigned threads : {2u, 4u, 8u}) {
    const cartograph::Graph parallel =
        cartograph::index_directory(crawl_dir(), cartograph::IndexOptions{threads});
    expect_graphs_identical(baseline, parallel);
  }
}

// The default (auto thread count) path must also match the single-threaded
// reference — parallelism is an invisible optimization, never a behavior change.
TEST(Pipeline, DefaultThreadCountMatchesSingleThreaded) {
  const cartograph::Graph baseline =
      cartograph::index_directory(crawl_dir(), cartograph::IndexOptions{1});
  const cartograph::Graph automatic = cartograph::index_directory(crawl_dir());
  expect_graphs_identical(baseline, automatic);
}
