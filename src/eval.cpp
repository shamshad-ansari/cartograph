#include "cartograph/eval.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "cartograph/extractor.hpp"
#include "cartograph/indexer.hpp"
#include "cartograph/parser.hpp"

namespace cartograph {
namespace {

// A copy of `in` sorted with duplicates removed — the set view the metrics are
// defined over, so a location repeated in either list is counted once.
std::vector<EvalLocation> as_set(const std::vector<EvalLocation>& in) {
  std::vector<EvalLocation> out = in;
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// The C source and header extensions, matching what the indexer crawls, so the
// grep baseline and the re-extraction see exactly the files the graph was built
// from.
bool is_c_source(const std::filesystem::path& path) {
  const std::filesystem::path ext = path.extension();
  return ext == ".c" || ext == ".h";
}

// `path` as a key relative to the corpus root, in generic (forward-slash) form,
// so ground truth and reports are independent of where the corpus lives.
std::string rel_key(const std::filesystem::path& path,
                    const std::filesystem::path& dir) {
  std::error_code ec;
  const std::filesystem::path r = std::filesystem::relative(path, dir, ec);
  return (ec ? path : r).generic_string();
}

// True where `c` can appear inside a C identifier — the character class that
// defines a whole-word (token) boundary for the grep baseline.
bool is_word_char(unsigned char c) { return std::isalnum(c) || c == '_'; }

// Read the whole file at `path` into `out`; false if it cannot be opened.
bool read_file(const std::filesystem::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

// Invoke `fn(path, source)` for every readable C source/header under `dir`, the
// same recursive, permission-tolerant crawl the indexer performs — so the files
// grep scans and the files re-extracted for who-calls match the indexed set.
template <typename Fn>
void for_each_c_file(const std::filesystem::path& dir, Fn&& fn) {
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      dir, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code stat_ec;
    if (!it->is_regular_file(stat_ec) || stat_ec || !is_c_source(it->path())) {
      continue;
    }
    std::string source;
    if (read_file(it->path(), source)) fn(it->path(), source);
  }
}

}  // namespace

std::string_view eval_query_name(EvalQuery query) {
  switch (query) {
    case EvalQuery::FindDefinition: return "find-definition";
    case EvalQuery::WhoCalls:       return "who-calls";
  }
  return "";
}

bool parse_eval_query(std::string_view token, EvalQuery& out) {
  if (token == "find-definition") {
    out = EvalQuery::FindDefinition;
    return true;
  }
  if (token == "who-calls") {
    out = EvalQuery::WhoCalls;
    return true;
  }
  return false;
}

Score score(const std::vector<EvalLocation>& expected,
            const std::vector<EvalLocation>& retrieved) {
  const std::vector<EvalLocation> e = as_set(expected);
  const std::vector<EvalLocation> r = as_set(retrieved);

  std::vector<EvalLocation> hits;
  std::set_intersection(e.begin(), e.end(), r.begin(), r.end(),
                        std::back_inserter(hits));

  Score s;
  s.relevant = e.size();
  s.retrieved = r.size();
  s.true_positives = hits.size();
  // Empty retrieved set: nothing wrong was returned, so precision is vacuously 1.
  // Empty expected set: nothing could be missed, so recall is vacuously 1.
  s.precision = r.empty() ? 1.0
                          : static_cast<double>(s.true_positives) /
                                static_cast<double>(s.retrieved);
  s.recall = e.empty() ? 1.0
                       : static_cast<double>(s.true_positives) /
                             static_cast<double>(s.relevant);
  return s;
}

std::vector<EvalLocation> grep_search(const std::filesystem::path& dir,
                                      const std::string& symbol) {
  std::vector<EvalLocation> out;
  if (symbol.empty()) return out;

  for_each_c_file(dir, [&](const std::filesystem::path& path,
                           const std::string& source) {
    const std::string key = rel_key(path, dir);
    std::uint32_t line = 1;
    std::size_t line_start = 0;
    // Walk the buffer line by line; a line is a hit if `symbol` occurs on it as a
    // whole token (bounded by non-identifier characters), which `grep -n` would
    // print once regardless of how many times it matches.
    for (std::size_t i = 0; i <= source.size(); ++i) {
      if (i == source.size() || source[i] == '\n') {
        const std::string_view text(source.data() + line_start, i - line_start);
        for (std::size_t pos = text.find(symbol); pos != std::string_view::npos;
             pos = text.find(symbol, pos + 1)) {
          const bool left = pos == 0 ||
                            !is_word_char(static_cast<unsigned char>(text[pos - 1]));
          const std::size_t after = pos + symbol.size();
          const bool right =
              after >= text.size() ||
              !is_word_char(static_cast<unsigned char>(text[after]));
          if (left && right) {
            out.push_back({key, line});
            break;  // one location per matching line
          }
        }
        ++line;
        line_start = i + 1;
      }
    }
  });
  return as_set(out);
}

std::vector<EvalLocation> cartograph_query(const Graph& graph,
                                           const std::filesystem::path& dir,
                                           EvalQuery query,
                                           const std::string& symbol) {
  std::vector<EvalLocation> out;

  // find-definition: exactly the function-definition nodes carrying the name.
  if (query == EvalQuery::FindDefinition) {
    for (const NodeId id : graph.nodes_named(symbol)) {
      const Node& n = graph.node(id);
      if (n.kind == NodeKind::Function) out.push_back({rel_key(n.file, dir), n.line});
    }
    return as_set(out);
  }

  // who-calls: the resolved call sites. The call-site line is not stored on the
  // CALLS edge, so we recover it by re-extracting calls from the source — but the
  // *resolution* verdict is the graph's own: a call site counts only if the graph
  // linked its enclosing function to a definition of `symbol`. So static shadowing
  // and unresolved names are honoured, never re-guessed here.
  std::vector<NodeId> targets;
  for (const NodeId id : graph.nodes_named(symbol)) {
    if (graph.node(id).kind == NodeKind::Function) targets.push_back(id);
  }
  if (targets.empty()) return out;

  const auto enclosing_is_linked = [&](const std::string& caller,
                                       const std::string& file) {
    // The caller's Function node in this translation unit (unique by name/file).
    NodeId caller_node = 0;
    bool found = false;
    for (const NodeId id : graph.nodes_named(caller)) {
      const Node& n = graph.node(id);
      if (n.kind == NodeKind::Function && n.file == file) {
        caller_node = id;
        found = true;
        break;
      }
    }
    if (!found) return false;
    for (const NodeId t : targets) {
      const std::vector<NodeId>& cs = graph.callers_of(t);
      if (std::find(cs.begin(), cs.end(), caller_node) != cs.end()) return true;
    }
    return false;
  };

  Parser parser;
  for_each_c_file(dir, [&](const std::filesystem::path& path,
                           const std::string& source) {
    const Tree tree = parser.parse(source);
    if (tree.has_error()) return;  // the indexer skipped it, so it holds no nodes
    for (CallFact& call : extract_calls(tree, source)) {
      if (call.callee == symbol && enclosing_is_linked(call.caller, path.string())) {
        out.push_back({rel_key(path, dir), call.line});
      }
    }
  });
  return as_set(out);
}

EvalReport run_eval(const std::filesystem::path& dir,
                    const std::vector<GroundTruth>& truth) {
  EvalReport report;
  report.corpus = dir;

  // Index once; every query is answered against this single graph.
  const Graph graph = index_directory(dir);

  double grep_p = 0, grep_r = 0, carto_p = 0, carto_r = 0;
  for (const GroundTruth& entry : truth) {
    EvalRow row;
    row.truth = entry;
    row.grep = score(entry.expected, grep_search(dir, entry.symbol));
    row.cartograph =
        score(entry.expected,
              cartograph_query(graph, dir, entry.query, entry.symbol));
    grep_p += row.grep.precision;
    grep_r += row.grep.recall;
    carto_p += row.cartograph.precision;
    carto_r += row.cartograph.recall;
    report.rows.push_back(std::move(row));
  }

  const double n = static_cast<double>(report.rows.size());
  report.grep.tool = "grep";
  report.cartograph.tool = "cartograph";
  report.grep.queries = report.cartograph.queries = report.rows.size();
  if (n > 0) {
    report.grep.precision = grep_p / n;
    report.grep.recall = grep_r / n;
    report.cartograph.precision = carto_p / n;
    report.cartograph.recall = carto_r / n;
  }
  return report;
}

std::vector<GroundTruth> load_ground_truth(const std::filesystem::path& file) {
  std::ifstream in(file);
  if (!in) {
    throw std::runtime_error("cannot open ground-truth file: " + file.string());
  }

  std::vector<GroundTruth> truth;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    std::istringstream ls(line);
    std::string query_token;
    if (!(ls >> query_token) || query_token[0] == '#') continue;  // blank/comment

    const auto fail = [&](const std::string& why) {
      throw std::runtime_error(file.string() + ":" + std::to_string(line_no) +
                               ": " + why);
    };

    GroundTruth entry;
    if (!parse_eval_query(query_token, entry.query)) {
      fail("unknown query '" + query_token + "'");
    }
    if (!(ls >> entry.symbol)) fail("missing symbol");

    // Remaining tokens are file:line locations; a line may legitimately have none.
    std::string tok;
    while (ls >> tok) {
      const std::size_t colon = tok.rfind(':');
      if (colon == std::string::npos) fail("expected file:line, got '" + tok + "'");
      EvalLocation loc;
      loc.file = tok.substr(0, colon);
      const std::string num = tok.substr(colon + 1);
      if (loc.file.empty() || num.empty() ||
          num.find_first_not_of("0123456789") != std::string::npos) {
        fail("expected file:line, got '" + tok + "'");
      }
      loc.line = static_cast<std::uint32_t>(std::stoul(num));
      entry.expected.push_back(std::move(loc));
    }
    truth.push_back(std::move(entry));
  }
  return truth;
}

void write_eval_summary(const EvalReport& report, std::ostream& out) {
  char line[256];

  out << "cartograph precision/recall vs grep — " << report.corpus.string()
      << "\n";
  out << "ground truth: " << report.rows.size()
      << " labelled queries (find-definition, who-calls)\n\n";

  // The framing (ADR-0010): grep is the baseline because it is the default
  // navigation tool, not a rival to beat. cscope/GNU GLOBAL are acknowledged.
  out << "grep/ripgrep is the default code-navigation tool a coding agent reaches\n"
         "for today; this measures the structural precision cartograph adds on top\n"
         "of it — plus a graph API and MCP integration. cscope and GNU GLOBAL are\n"
         "the specialized C indexers, noted but not the baseline here.\n\n";

  out << "per-query (P precision, R recall):\n";
  std::snprintf(line, sizeof(line),
                "  %-16s %-14s %8s %8s   %8s %8s\n", "QUERY", "SYMBOL",
                "grep P", "grep R", "carto P", "carto R");
  out << line;
  for (const EvalRow& row : report.rows) {
    std::snprintf(line, sizeof(line),
                  "  %-16s %-14s %8.2f %8.2f   %8.2f %8.2f\n",
                  std::string(eval_query_name(row.truth.query)).c_str(),
                  row.truth.symbol.c_str(), row.grep.precision, row.grep.recall,
                  row.cartograph.precision, row.cartograph.recall);
    out << line;
  }

  out << "\naggregate (mean over " << report.rows.size() << " queries):\n";
  std::snprintf(line, sizeof(line), "  %-12s %10s %8s\n", "tool", "precision",
                "recall");
  out << line;
  std::snprintf(line, sizeof(line), "  %-12s %10.2f %8.2f\n", "grep",
                report.grep.precision, report.grep.recall);
  out << line;
  std::snprintf(line, sizeof(line), "  %-12s %10.2f %8.2f\n", "cartograph",
                report.cartograph.precision, report.cartograph.recall);
  out << line;
}

namespace {

// One tool's Score as a JSON object body.
void write_score_json(const Score& s, std::ostream& out) {
  out << "{\"precision\": " << s.precision << ", \"recall\": " << s.recall
      << ", \"true_positives\": " << s.true_positives
      << ", \"retrieved\": " << s.retrieved << ", \"relevant\": " << s.relevant
      << "}";
}

}  // namespace

void write_eval_json(const EvalReport& report, std::ostream& out) {
  out << "{\n";
  out << "  \"corpus\": \"" << report.corpus.generic_string() << "\",\n";
  out << "  \"queries\": " << report.rows.size() << ",\n";
  out << "  \"aggregate\": {\n";
  out << "    \"grep\": {\"precision\": " << report.grep.precision
      << ", \"recall\": " << report.grep.recall << "},\n";
  out << "    \"cartograph\": {\"precision\": " << report.cartograph.precision
      << ", \"recall\": " << report.cartograph.recall << "}\n";
  out << "  },\n";
  out << "  \"rows\": [\n";
  for (std::size_t i = 0; i < report.rows.size(); ++i) {
    const EvalRow& row = report.rows[i];
    out << "    {\"query\": \"" << eval_query_name(row.truth.query)
        << "\", \"symbol\": \"" << row.truth.symbol << "\", \"grep\": ";
    write_score_json(row.grep, out);
    out << ", \"cartograph\": ";
    write_score_json(row.cartograph, out);
    out << "}" << (i + 1 < report.rows.size() ? "," : "") << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

}  // namespace cartograph
