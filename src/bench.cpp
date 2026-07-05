#include "cartograph/bench.hpp"

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

#include "cartograph/graph.hpp"
#include "cartograph/graph_io.hpp"
#include "cartograph/indexer.hpp"
#include "cartograph/parallel.hpp"

namespace cartograph {
namespace {

using Clock = std::chrono::steady_clock;

// The worker counts to probe for the scaling curve: 1, 2, 4, … up to the
// hardware concurrency, with the exact hardware count appended when it is not
// already a power of two. Doubling keeps the curve short and readable while
// still showing where speedup flattens.
std::vector<unsigned> scaling_thread_counts() {
  const unsigned hw = default_thread_count();
  std::vector<unsigned> counts;
  for (unsigned t = 1; t < hw; t *= 2) counts.push_back(t);
  counts.push_back(hw);
  return counts;
}

// Individual queries run in microseconds, near the timer's own noise floor, so a
// single reading is unreliable. Each sampled query is timed a few times and the
// minimum is kept — the run least perturbed by scheduling and cache misses — and
// it is the distribution of those minima across symbols that the percentiles
// describe.
constexpr int kQueryRepeats = 5;

// The same file filter the indexer applies, mirrored here to size the throughput
// denominators (files/lines/bytes) over exactly the inputs indexing attempts.
bool is_c_source(const std::filesystem::path& path) {
  const std::filesystem::path ext = path.extension();
  return ext == ".c" || ext == ".h";
}

// Process peak resident set size in bytes. `ru_maxrss` is reported in bytes on
// macOS but in kilobytes on Linux; normalize to bytes.
std::size_t peak_rss_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::size_t>(usage.ru_maxrss);
#else
  return static_cast<std::size_t>(usage.ru_maxrss) * 1024;
#endif
}

// Total files, bytes, and newline count over the C sources under `dir`. Measured
// outside the timed index region, so re-reading the files here does not affect
// the wall-clock numbers. LOC is the newline count — the conventional, cheap
// definition, and honest about what was scanned.
void measure_input_size(const std::filesystem::path& dir, IndexBenchmark& out) {
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      dir, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code stat_ec;
    if (!it->is_regular_file(stat_ec) || stat_ec || !is_c_source(it->path())) {
      continue;
    }
    std::ifstream in(it->path(), std::ios::binary);
    if (!in) continue;
    ++out.files;

    char buffer[64 * 1024];
    do {
      in.read(buffer, sizeof(buffer));
      const std::streamsize got = in.gcount();
      out.bytes += static_cast<std::size_t>(got);
      out.lines += static_cast<std::size_t>(
          std::count(buffer, buffer + got, '\n'));
    } while (in);
  }
}

// The p-th percentile (p in [0, 1]) of `values` by linear interpolation between
// the two nearest ranks. `values` is sorted in place. Returns 0 for an empty
// sample.
double percentile(std::vector<double>& values, double p) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  if (values.size() == 1) return values.front();
  const double rank = p * static_cast<double>(values.size() - 1);
  const std::size_t lo = static_cast<std::size_t>(rank);
  const double frac = rank - static_cast<double>(lo);
  if (lo + 1 < values.size()) {
    return values[lo] + frac * (values[lo + 1] - values[lo]);
  }
  return values[lo];
}

// A deterministic, evenly-spaced sample of up to `count` distinct function names
// from the graph. Sorting first makes the selection independent of directory
// iteration order, so the query percentiles are stable across runs.
std::vector<std::string> sample_function_names(const Graph& graph,
                                               std::size_t count) {
  std::vector<std::string> names;
  for (NodeId id = 0; id < graph.size(); ++id) {
    if (graph.node(id).kind == NodeKind::Function) {
      names.emplace_back(graph.node(id).name);
    }
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());

  if (count == 0 || names.size() <= count) return names;

  std::vector<std::string> sampled;
  sampled.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    // Evenly spaced indices across [0, names.size()), inclusive of both ends.
    const std::size_t idx = i * (names.size() - 1) / (count - 1);
    sampled.push_back(names[idx]);
  }
  sampled.erase(std::unique(sampled.begin(), sampled.end()), sampled.end());
  return sampled;
}

// Time `query` over each sampled symbol and summarize the latency distribution.
// `query` runs the operation for one name; its return value is used only to keep
// the optimizer from eliding the work.
template <typename Query>
QueryLatency measure_query(const std::string& name,
                           const std::vector<std::string>& symbols,
                           Query&& query) {
  QueryLatency out;
  out.name = name;

  std::vector<double> latencies;
  latencies.reserve(symbols.size());
  volatile std::size_t sink = 0;
  for (const std::string& symbol : symbols) {
    double best = 0.0;
    for (int r = 0; r < kQueryRepeats; ++r) {
      const Clock::time_point t0 = Clock::now();
      sink += query(symbol);
      const Clock::time_point t1 = Clock::now();
      const double us =
          std::chrono::duration<double, std::micro>(t1 - t0).count();
      if (r == 0 || us < best) best = us;
    }
    latencies.push_back(best);
  }
  (void)sink;

  out.samples = latencies.size();
  out.p50_us = percentile(latencies, 0.50);
  out.p95_us = percentile(latencies, 0.95);
  out.p99_us = percentile(latencies, 0.99);
  out.max_us = latencies.empty() ? 0.0 : latencies.back();  // sorted ascending
  return out;
}

}  // namespace

BenchmarkReport run_benchmark(const std::filesystem::path& dir,
                              const BenchmarkOptions& opts) {
  BenchmarkReport report;
  report.repo = dir.string();
  measure_input_size(dir, report.index);

  const int runs = std::max(1, opts.index_runs);
  report.index.runs = runs;

  // Index `runs` times, keeping the last graph to query. The median wall time is
  // reported — robust to the cold-cache first run and to scheduler jitter.
  std::vector<double> wall_ms;
  wall_ms.reserve(runs);
  Graph graph;
  for (int r = 0; r < runs; ++r) {
    const Clock::time_point t0 = Clock::now();
    graph = index_directory(dir);
    const Clock::time_point t1 = Clock::now();
    wall_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  report.index.wall_ms = percentile(wall_ms, 0.50);
  report.index.peak_rss_bytes = peak_rss_bytes();

  const double seconds = report.index.wall_ms / 1000.0;
  if (seconds > 0) {
    report.index.files_per_sec =
        static_cast<double>(report.index.files) / seconds;
    report.index.lines_per_sec =
        static_cast<double>(report.index.lines) / seconds;
  }

  // Thread-scaling curve (issue 0012): re-index at 1, 2, 4, … workers and report
  // each configuration's median wall time and its speedup over single-threaded.
  // The graph is identical at every count (a regression test guarantees it), so
  // this isolates the parallel pipeline's speedup from any change in output.
  if (opts.thread_scaling) {
    double baseline_ms = 0;
    for (const unsigned threads : scaling_thread_counts()) {
      std::vector<double> point_ms;
      point_ms.reserve(runs);
      for (int r = 0; r < runs; ++r) {
        const Clock::time_point t0 = Clock::now();
        const Graph g = index_directory(dir, IndexOptions{threads});
        const Clock::time_point t1 = Clock::now();
        point_ms.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
      }
      ThreadScalingPoint point;
      point.threads = threads;
      point.wall_ms = percentile(point_ms, 0.50);
      if (threads == 1) baseline_ms = point.wall_ms;
      point.speedup =
          point.wall_ms > 0 ? baseline_ms / point.wall_ms : 0.0;
      report.scaling.push_back(point);
    }
  }

  // Persistence: cold startup parses the whole repo (the median already measured
  // above); warm startup memory-maps the saved index instead. Save once, then
  // time `runs` mmap-loads and keep the median — the payoff ADR-0008 promises.
  report.persistence.cold_index_ms = report.index.wall_ms;
  const std::filesystem::path index_file =
      std::filesystem::temp_directory_path() /
      ("cartograph-bench-" + std::to_string(::getpid()) + ".idx");
  std::error_code save_ec;
  try {
    save_graph(graph, index_file);
    report.persistence.index_file_bytes =
        static_cast<std::size_t>(std::filesystem::file_size(index_file, save_ec));

    std::vector<double> load_ms;
    load_ms.reserve(runs);
    for (int r = 0; r < runs; ++r) {
      const Clock::time_point t0 = Clock::now();
      const Graph warm = load_graph(index_file);
      const Clock::time_point t1 = Clock::now();
      load_ms.push_back(
          std::chrono::duration<double, std::milli>(t1 - t0).count());
      (void)warm.size();  // keep the load from being elided
    }
    report.persistence.warm_load_ms = percentile(load_ms, 0.50);
    if (report.persistence.warm_load_ms > 0) {
      report.persistence.speedup =
          report.persistence.cold_index_ms / report.persistence.warm_load_ms;
    }
  } catch (const std::exception&) {
    // Leave the persistence numbers at zero if the scratch index cannot be
    // written; the rest of the benchmark still stands.
  }
  std::filesystem::remove(index_file, save_ec);

  // Query latency over a deterministic symbol sample. The operations mirror the
  // CLI query commands but run against the already-built graph, so they time the
  // query itself rather than a re-index.
  const std::vector<std::string> symbols =
      sample_function_names(graph, opts.query_samples);

  report.queries.push_back(measure_query(
      "find-definition", symbols, [&](const std::string& name) {
        std::size_t hits = 0;
        for (const NodeId id : graph.nodes_named(name)) {
          if (graph.node(id).kind == NodeKind::Function) ++hits;
        }
        return hits;
      }));

  report.queries.push_back(measure_query(
      "who-calls", symbols, [&](const std::string& name) {
        std::size_t hits = 0;
        for (const NodeId callee : graph.nodes_named(name)) {
          hits += graph.callers_of(callee).size();
        }
        return hits;
      }));

  report.queries.push_back(measure_query(
      "blast-radius", symbols, [&](const std::string& name) {
        return graph.transitive_callers(graph.nodes_named(name)).size();
      }));

  return report;
}

void write_json(const BenchmarkReport& report, std::ostream& out) {
  const IndexBenchmark& idx = report.index;
  out << "{\n";
  out << "  \"repo\": \"" << report.repo << "\",\n";
  out << "  \"index\": {\n";
  out << "    \"files\": " << idx.files << ",\n";
  out << "    \"lines\": " << idx.lines << ",\n";
  out << "    \"bytes\": " << idx.bytes << ",\n";
  out << "    \"runs\": " << idx.runs << ",\n";
  out << "    \"wall_ms\": " << idx.wall_ms << ",\n";
  out << "    \"files_per_sec\": " << idx.files_per_sec << ",\n";
  out << "    \"lines_per_sec\": " << idx.lines_per_sec << ",\n";
  out << "    \"peak_rss_bytes\": " << idx.peak_rss_bytes << "\n";
  out << "  },\n";
  out << "  \"queries\": {\n";
  for (std::size_t i = 0; i < report.queries.size(); ++i) {
    const QueryLatency& q = report.queries[i];
    out << "    \"" << q.name << "\": {"
        << "\"samples\": " << q.samples << ", "
        << "\"p50_us\": " << q.p50_us << ", "
        << "\"p95_us\": " << q.p95_us << ", "
        << "\"p99_us\": " << q.p99_us << ", "
        << "\"max_us\": " << q.max_us << "}"
        << (i + 1 < report.queries.size() ? "," : "") << "\n";
  }
  out << "  },\n";
  out << "  \"scaling\": [";
  for (std::size_t i = 0; i < report.scaling.size(); ++i) {
    const ThreadScalingPoint& s = report.scaling[i];
    out << (i == 0 ? "\n" : ",\n")
        << "    {\"threads\": " << s.threads << ", "
        << "\"wall_ms\": " << s.wall_ms << ", "
        << "\"speedup\": " << s.speedup << "}";
  }
  out << (report.scaling.empty() ? "],\n" : "\n  ],\n");
  const PersistenceBenchmark& p = report.persistence;
  out << "  \"persistence\": {\n";
  out << "    \"cold_index_ms\": " << p.cold_index_ms << ",\n";
  out << "    \"warm_load_ms\": " << p.warm_load_ms << ",\n";
  out << "    \"index_file_bytes\": " << p.index_file_bytes << ",\n";
  out << "    \"speedup\": " << p.speedup << "\n";
  out << "  }\n";
  out << "}\n";
}

namespace {

// A byte count rendered in the largest unit that keeps it below 1024, e.g.
// "210.3 MB" — for the human summary only.
std::string human_bytes(std::size_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
  return buffer;
}

}  // namespace

void write_summary(const BenchmarkReport& report, std::ostream& out) {
  const IndexBenchmark& idx = report.index;
  char line[128];

  out << "cartograph benchmark — " << report.repo << "\n";
  out << "index:\n";
  out << "  files            " << idx.files << "\n";
  out << "  lines            " << idx.lines << "\n";
  std::snprintf(line, sizeof(line), "  wall (median)    %.2f ms   (%d runs)\n",
                idx.wall_ms, idx.runs);
  out << line;
  std::snprintf(line, sizeof(line),
                "  throughput       %.0f files/s, %.0f lines/s\n",
                idx.files_per_sec, idx.lines_per_sec);
  out << line;
  out << "  peak RSS         " << human_bytes(idx.peak_rss_bytes) << "\n";

  const std::size_t samples =
      report.queries.empty() ? 0 : report.queries.front().samples;
  out << "query latency (us, over " << samples << " symbols):\n";
  for (const QueryLatency& q : report.queries) {
    std::snprintf(line, sizeof(line),
                  "  %-16s p50 %8.2f   p95 %8.2f   p99 %8.2f\n", q.name.c_str(),
                  q.p50_us, q.p95_us, q.p99_us);
    out << line;
  }

  if (!report.scaling.empty()) {
    out << "thread scaling (index wall vs workers):\n";
    for (const ThreadScalingPoint& s : report.scaling) {
      std::snprintf(line, sizeof(line),
                    "  %2u threads       %8.2f ms   %5.2fx\n", s.threads,
                    s.wall_ms, s.speedup);
      out << line;
    }
  }

  const PersistenceBenchmark& p = report.persistence;
  out << "startup (cold parse vs warm mmap load):\n";
  std::snprintf(line, sizeof(line), "  cold (full parse)  %8.2f ms\n",
                p.cold_index_ms);
  out << line;
  std::snprintf(line, sizeof(line), "  warm (mmap load)   %8.2f ms   %5.2fx\n",
                p.warm_load_ms, p.speedup);
  out << line;
  out << "  index file         " << human_bytes(p.index_file_bytes) << "\n";
}

}  // namespace cartograph
