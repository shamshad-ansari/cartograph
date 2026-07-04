#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace cartograph {

// Knobs for a benchmark run. Defaults are chosen so a single invocation produces
// stable numbers on a real repository without taking long.
struct BenchmarkOptions {
  // Measured index iterations; the reported wall time is their median, which is
  // robust to the outlier first run (cold file cache) and to scheduler jitter.
  int index_runs = 5;
  // How many distinct function symbols to sample for query latency. The sample
  // is deterministic (evenly spaced over the sorted names) so the percentiles
  // are stable across runs; capped at the number of functions in the repo.
  std::size_t query_samples = 50;
};

// Index-phase throughput and footprint over the target repository.
struct IndexBenchmark {
  std::size_t files = 0;   // C source/header files the indexer attempted
  std::size_t lines = 0;   // total newline count across those files
  std::size_t bytes = 0;   // total bytes across those files
  int runs = 0;            // measured iterations behind `wall_ms`
  double wall_ms = 0;      // median index wall-clock, milliseconds
  double files_per_sec = 0;
  double lines_per_sec = 0;
  std::size_t peak_rss_bytes = 0;  // process peak resident set size
};

// Latency distribution for one query kind over the sampled symbols. Percentiles
// are in microseconds.
struct QueryLatency {
  std::string name;        // "find-definition", "who-calls", "blast-radius"
  std::size_t samples = 0;
  double p50_us = 0;
  double p95_us = 0;
  double p99_us = 0;
  double max_us = 0;
};

// A full benchmark report: index throughput plus per-query latency, ready to be
// emitted as JSON (machine-readable, for tracking across changes) or as a
// human-readable summary.
struct BenchmarkReport {
  std::string repo;
  IndexBenchmark index;
  std::vector<QueryLatency> queries;
};

// Run the benchmark over `dir`: index it `opts.index_runs` times to measure
// throughput and peak RSS, then time `find-definition`, `who-calls`, and
// `blast-radius` over a deterministic sample of function symbols.
BenchmarkReport run_benchmark(const std::filesystem::path& dir,
                              const BenchmarkOptions& opts = {});

// Emit `report` as JSON on `out` — the machine-readable form for tracking.
void write_json(const BenchmarkReport& report, std::ostream& out);

// Emit `report` as an aligned human-readable summary on `out`.
void write_summary(const BenchmarkReport& report, std::ostream& out);

}  // namespace cartograph
