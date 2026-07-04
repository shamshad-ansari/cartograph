#include <filesystem>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "cartograph/bench.hpp"

namespace {

// The `calls` fixture has several functions across files with CALLS edges — real
// content for the query-latency sample to exercise.
std::filesystem::path calls_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "calls";
}

cartograph::BenchmarkReport bench() {
  cartograph::BenchmarkOptions opts;
  opts.index_runs = 3;
  opts.query_samples = 8;
  return cartograph::run_benchmark(calls_dir(), opts);
}

}  // namespace

// The index phase reports a sane throughput and footprint: files and lines were
// counted, the requested number of runs was honored, and peak RSS is nonzero.
TEST(Bench, MeasuresIndexThroughput) {
  const cartograph::BenchmarkReport report = bench();

  EXPECT_GT(report.index.files, 0u);
  EXPECT_GT(report.index.lines, 0u);
  EXPECT_GT(report.index.bytes, 0u);
  EXPECT_EQ(report.index.runs, 3);
  EXPECT_GE(report.index.wall_ms, 0.0);
  EXPECT_GT(report.index.peak_rss_bytes, 0u);
}

// All three core queries are measured, over a nonempty symbol sample, with the
// percentiles in non-decreasing order (p50 <= p95 <= p99).
TEST(Bench, MeasuresQueryLatencyPercentiles) {
  const cartograph::BenchmarkReport report = bench();

  ASSERT_EQ(report.queries.size(), 3u);
  EXPECT_EQ(report.queries[0].name, "find-definition");
  EXPECT_EQ(report.queries[1].name, "who-calls");
  EXPECT_EQ(report.queries[2].name, "blast-radius");

  for (const cartograph::QueryLatency& q : report.queries) {
    EXPECT_GT(q.samples, 0u) << q.name << " sampled no symbols";
    EXPECT_LE(q.p50_us, q.p95_us) << q.name;
    EXPECT_LE(q.p95_us, q.p99_us) << q.name;
    EXPECT_LE(q.p99_us, q.max_us + 1e-9) << q.name;
  }
}

// The deterministic symbol sample makes the harness stable run-to-run: the same
// number of symbols is measured each time (wall-clock timing aside).
TEST(Bench, SamplesDeterministically) {
  const cartograph::BenchmarkReport a = bench();
  const cartograph::BenchmarkReport b = bench();

  ASSERT_EQ(a.queries.size(), b.queries.size());
  for (std::size_t i = 0; i < a.queries.size(); ++i) {
    EXPECT_EQ(a.queries[i].name, b.queries[i].name);
    EXPECT_EQ(a.queries[i].samples, b.queries[i].samples);
  }
  EXPECT_EQ(a.index.files, b.index.files);
  EXPECT_EQ(a.index.lines, b.index.lines);
}

// JSON output carries the machine-readable keys a tracking pipeline reads.
TEST(Bench, EmitsJson) {
  const cartograph::BenchmarkReport report = bench();
  std::ostringstream out;
  cartograph::write_json(report, out);
  const std::string json = out.str();

  EXPECT_NE(json.find("\"index\""), std::string::npos);
  EXPECT_NE(json.find("\"files_per_sec\""), std::string::npos);
  EXPECT_NE(json.find("\"peak_rss_bytes\""), std::string::npos);
  EXPECT_NE(json.find("\"blast-radius\""), std::string::npos);
  EXPECT_NE(json.find("\"p99_us\""), std::string::npos);
}
