#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cartograph/bench.hpp"
#include "cartograph/eval.hpp"
#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"
#include "cartograph/parser.hpp"

namespace {

int usage(std::ostream& out) {
  out << "usage: cartograph <command> [args]\n\n"
         "commands:\n"
         "  parse <file.c>                       parse a C file and print its "
         "syntax tree\n"
         "  find-definition <name> --dir <path>  print file:line of each "
         "definition of <name>\n"
         "  find-declarations <name> --dir <path>  print file:line of each "
         "declaration of <name>\n"
         "  who-calls <name> --dir <path>        print file:line of each "
         "function that calls <name>\n"
         "  blast-radius <name> --dir <path>     print file:line of every "
         "direct and indirect caller of <name>\n"
         "  include-graph <file> --dir <path>    print the files <file> "
         "includes and the files that include it\n"
         "  who-uses-type <name> --dir <path>    print file:line of each "
         "function that references type <name>\n"
         "  index <path>                         recursively index <path> and "
         "print file/node/edge counts\n"
         "  bench <path> [--json] [--runs N] [--sample N]\n"
         "                                       measure index throughput, peak "
         "RSS, and query-latency percentiles over <path>\n"
         "  eval <path> --truth <file> [--json]  score cartograph vs grep for "
         "precision/recall over a labelled ground-truth set\n";
  return 2;
}

// Parse the shared `<name> --dir <path>` argument shape used by the graph
// queries. On success fills `name`/`dir` and returns true; otherwise reports the
// problem and returns false.
bool parse_name_and_dir(const std::vector<std::string_view>& args,
                        std::string_view command, std::string_view& name,
                        std::string_view& dir) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--dir") {
      if (++i >= args.size()) {
        std::cerr << "cartograph: '--dir' expects a path\n";
        return false;
      }
      dir = args[i];
    } else if (name.empty()) {
      name = args[i];
    } else {
      std::cerr << "cartograph: unexpected argument '" << args[i] << "'\n";
      return false;
    }
  }
  if (name.empty() || dir.empty()) {
    std::cerr << "cartograph: '" << command
              << "' expects <name> --dir <path>\n";
    return false;
  }
  return true;
}

// Read an entire file into a string. Returns false if it cannot be opened.
bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

int cmd_parse(std::string_view path) {
  std::string source;
  if (!read_file(std::string(path), source)) {
    std::cerr << "cartograph: cannot open '" << path << "'\n";
    return 1;
  }

  cartograph::Parser parser;
  cartograph::Tree tree = parser.parse(source);
  if (tree.empty()) {
    std::cerr << "cartograph: failed to parse '" << path << "'\n";
    return 1;
  }

  cartograph::dump_tree(tree, std::cout);
  return 0;
}

// Print any resolution diagnostics and skipped-file warnings to stderr, so an
// ambiguous link error or an unparseable file is visible to the user without
// polluting the query result on stdout.
void report_diagnostics(const cartograph::Graph& graph) {
  for (const cartograph::SkippedFile& s : graph.skipped_files()) {
    std::cerr << "cartograph: warning: skipped " << s.path << " (" << s.reason
              << ")\n";
  }
  for (const cartograph::Diagnostic& d : graph.diagnostics()) {
    std::cerr << "cartograph: warning: call to '" << d.callee << "' at "
              << d.caller_file << ':' << d.caller_line << " has "
              << d.candidates.size()
              << " conflicting external definitions:\n";
    for (const cartograph::NodeId id : d.candidates) {
      const cartograph::Node& node = graph.node(id);
      std::cerr << "  " << node.file << ':' << node.line << '\n';
    }
  }
}

int cmd_find_definition(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "find-definition", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);

  // Collect and sort by (file, line) so output is deterministic regardless of
  // directory iteration order. Only definitions (a body) count here; prototypes
  // are FunctionDecl nodes answered by find-declarations.
  std::vector<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    const cartograph::Node& node = graph.node(id);
    if (node.kind != cartograph::NodeKind::Function) continue;
    hits.emplace_back(node.file, node.line);
  }
  std::sort(hits.begin(), hits.end());

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

int cmd_find_declarations(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "find-declarations", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);

  // Only prototypes (FunctionDecl) — the answer to "where is this declared?".
  // Sorted by (file, line) for output independent of iteration order.
  std::vector<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    const cartograph::Node& node = graph.node(id);
    if (node.kind != cartograph::NodeKind::FunctionDecl) continue;
    hits.emplace_back(node.file, node.line);
  }
  std::sort(hits.begin(), hits.end());

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

int cmd_who_calls(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "who-calls", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);
  report_diagnostics(graph);

  // A caller is listed once even if it calls the target repeatedly or the name
  // has several definitions; std::set both dedupes and sorts by (file, line).
  std::set<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::NodeId callee : graph.nodes_named(name)) {
    for (const cartograph::NodeId caller : graph.callers_of(callee)) {
      const cartograph::Node& node = graph.node(caller);
      hits.emplace(node.file, node.line);
    }
  }

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

int cmd_blast_radius(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "blast-radius", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);
  report_diagnostics(graph);

  // Seed the reverse traversal with every node carrying the name — its
  // definitions — and collect the transitive callers. Sort by (file, line) so
  // output is deterministic and matches the sibling query commands; the same
  // caller reached by several paths is already emitted once by the graph walk.
  const std::vector<cartograph::Caller> reached =
      graph.transitive_callers(graph.nodes_named(name));

  std::set<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::Caller& c : reached) {
    const cartograph::Node& node = graph.node(c.node);
    hits.emplace(node.file, node.line);
  }

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

int cmd_include_graph(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "include-graph", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);

  // A file is addressed by its basename; there is one File node per indexed file,
  // so at most one match in this non-recursive slice.
  std::vector<cartograph::NodeId> targets;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    if (graph.node(id).kind == cartograph::NodeKind::File) targets.push_back(id);
  }
  if (targets.empty()) {
    std::cerr << "cartograph: no indexed file named '" << name << "'\n";
    return 1;
  }

  // Gather both directions and any unresolved (system/external) includes. Sets
  // sort and dedupe so output is deterministic and a file included twice lists
  // once.
  std::set<std::string> includes;
  std::set<std::string> included_by;
  std::set<std::pair<std::string, bool>> unresolved;  // (target, is_system)
  for (const cartograph::NodeId target : targets) {
    for (const cartograph::NodeId id : graph.includes_of(target)) {
      includes.insert(graph.node(id).file);
    }
    for (const cartograph::NodeId id : graph.included_by(target)) {
      included_by.insert(graph.node(id).file);
    }
    for (const cartograph::UnresolvedInclude& u : graph.unresolved_includes()) {
      if (u.includer == target) unresolved.emplace(u.target, u.is_system);
    }
  }

  std::cout << "includes:\n";
  for (const std::string& path : includes) std::cout << "  " << path << '\n';
  for (const auto& [target, is_system] : unresolved) {
    std::cout << "  " << target << (is_system ? "  (unresolved: system)\n"
                                              : "  (unresolved: external)\n");
  }
  std::cout << "included by:\n";
  for (const std::string& path : included_by) std::cout << "  " << path << '\n';
  return 0;
}

int cmd_index(const std::vector<std::string_view>& args) {
  if (args.size() != 1) {
    std::cerr << "cartograph: 'index' expects exactly one directory argument\n";
    return usage(std::cerr);
  }
  const std::filesystem::path root(args[0]);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << args[0] << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);
  report_diagnostics(graph);  // skipped-file warnings, to stderr

  // File count is the number of File nodes; the crawl makes one per indexed file.
  std::size_t files = 0;
  for (cartograph::NodeId id = 0; id < graph.size(); ++id) {
    if (graph.node(id).kind == cartograph::NodeKind::File) ++files;
  }

  std::cout << "files:   " << files << '\n'
            << "nodes:   " << graph.size() << '\n'
            << "edges:   " << graph.edge_count() << '\n'
            << "skipped: " << graph.skipped_files().size() << '\n';
  return 0;
}

int cmd_who_uses_type(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "who-uses-type", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);

  // Seed with every type node carrying the name — a tag and a typedef can share
  // one — and collect their referencing functions. A function is listed once
  // even if it references the type repeatedly or via several matching nodes;
  // std::set both dedupes and sorts by (file, line).
  std::set<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::NodeId type : graph.nodes_named(name)) {
    if (!cartograph::is_type_node(graph.node(type).kind)) continue;
    for (const cartograph::NodeId user : graph.users_of(type)) {
      const cartograph::Node& node = graph.node(user);
      hits.emplace(node.file, node.line);
    }
  }

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

int cmd_bench(const std::vector<std::string_view>& args) {
  std::string_view path;
  bool as_json = false;
  cartograph::BenchmarkOptions opts;

  // Parse `<path> [--json] [--runs N] [--sample N]`. --runs and --sample take an
  // integer; --json toggles machine-readable output. The first bare argument is
  // the target directory.
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--json") {
      as_json = true;
    } else if (args[i] == "--runs" || args[i] == "--sample") {
      const std::string_view flag = args[i];
      if (++i >= args.size()) {
        std::cerr << "cartograph: '" << flag << "' expects a number\n";
        return usage(std::cerr);
      }
      int value = 0;
      const auto [ptr, ec] = std::from_chars(
          args[i].data(), args[i].data() + args[i].size(), value);
      if (ec != std::errc{} || ptr != args[i].data() + args[i].size() ||
          value < 0) {
        std::cerr << "cartograph: '" << flag << "' expects a non-negative number, got '"
                  << args[i] << "'\n";
        return 1;
      }
      if (flag == "--runs") {
        opts.index_runs = value;
      } else {
        opts.query_samples = static_cast<std::size_t>(value);
      }
    } else if (path.empty()) {
      path = args[i];
    } else {
      std::cerr << "cartograph: unexpected argument '" << args[i] << "'\n";
      return usage(std::cerr);
    }
  }

  if (path.empty()) {
    std::cerr << "cartograph: 'bench' expects a directory path\n";
    return usage(std::cerr);
  }
  const std::filesystem::path root(path);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << path << "'\n";
    return 1;
  }

  const cartograph::BenchmarkReport report =
      cartograph::run_benchmark(root, opts);
  if (as_json) {
    cartograph::write_json(report, std::cout);
  } else {
    cartograph::write_summary(report, std::cout);
  }
  return 0;
}

int cmd_eval(const std::vector<std::string_view>& args) {
  std::string_view path;
  std::string_view truth;
  bool as_json = false;

  // Parse `<path> --truth <file> [--json]`. The first bare argument is the corpus
  // directory; --truth names the labelled ground-truth file.
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--json") {
      as_json = true;
    } else if (args[i] == "--truth") {
      if (++i >= args.size()) {
        std::cerr << "cartograph: '--truth' expects a file\n";
        return usage(std::cerr);
      }
      truth = args[i];
    } else if (path.empty()) {
      path = args[i];
    } else {
      std::cerr << "cartograph: unexpected argument '" << args[i] << "'\n";
      return usage(std::cerr);
    }
  }

  if (path.empty() || truth.empty()) {
    std::cerr << "cartograph: 'eval' expects <path> --truth <file>\n";
    return usage(std::cerr);
  }
  const std::filesystem::path root(path);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << path << "'\n";
    return 1;
  }

  try {
    const std::vector<cartograph::GroundTruth> ground_truth =
        cartograph::load_ground_truth(std::filesystem::path(truth));
    const cartograph::EvalReport report =
        cartograph::run_eval(root, ground_truth);
    if (as_json) {
      cartograph::write_eval_json(report, std::cout);
    } else {
      cartograph::write_eval_summary(report, std::cout);
    }
  } catch (const std::exception& e) {
    std::cerr << "cartograph: " << e.what() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage(std::cerr);

  const std::string_view command = argv[1];
  if (command == "parse") {
    if (argc != 3) {
      std::cerr << "cartograph: 'parse' expects exactly one file argument\n";
      return usage(std::cerr);
    }
    return cmd_parse(argv[2]);
  }
  if (command == "find-definition") {
    return cmd_find_definition(
        std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "find-declarations") {
    return cmd_find_declarations(
        std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "who-calls") {
    return cmd_who_calls(std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "blast-radius") {
    return cmd_blast_radius(
        std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "include-graph") {
    return cmd_include_graph(
        std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "who-uses-type") {
    return cmd_who_uses_type(
        std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "index") {
    return cmd_index(std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "bench") {
    return cmd_bench(std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "eval") {
    return cmd_eval(std::vector<std::string_view>(argv + 2, argv + argc));
  }

  std::cerr << "cartograph: unknown command '" << command << "'\n";
  return usage(std::cerr);
}
