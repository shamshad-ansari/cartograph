#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

std::filesystem::path crawl_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "crawl";
}

// Whether a node of `kind` named `name` exists in the graph.
bool has_node(const cartograph::Graph& graph, const std::string& name,
              cartograph::NodeKind kind) {
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    if (graph.node(id).kind == kind) return true;
  }
  return false;
}

}  // namespace

// Discovery recurses the whole tree: definitions and declarations in nested
// subdirectories (sub/, sub/deep/) are all indexed, and a call resolves across
// directory boundaries.
TEST(Crawl, IndexesFilesAcrossNestedDirectories) {
  const cartograph::Graph graph = cartograph::index_directory(crawl_dir());

  EXPECT_TRUE(has_node(graph, "fa", cartograph::NodeKind::Function));     // a.c
  EXPECT_TRUE(has_node(graph, "fb", cartograph::NodeKind::Function));     // sub/b.c
  EXPECT_TRUE(has_node(graph, "fc", cartograph::NodeKind::FunctionDecl)); // sub/deep/c.h

  // fb (sub/b.c) calls fa (a.c) — a CALLS edge resolved across directories.
  // (b.c also carries an `fa` prototype, so select the Function definition.)
  cartograph::NodeId fa_def = 0;
  bool found = false;
  for (const cartograph::NodeId id : graph.nodes_named("fa")) {
    if (graph.node(id).kind == cartograph::NodeKind::Function) {
      fa_def = id;
      found = true;
    }
  }
  ASSERT_TRUE(found);
  const std::vector<cartograph::NodeId>& callers = graph.callers_of(fa_def);
  ASSERT_EQ(callers.size(), 1u);
  EXPECT_EQ(graph.node(callers.front()).name, "fb");
}

// A file with a syntax error is skipped with a diagnostic, and indexing of the
// remaining files completes — the malformed file's definition never enters the
// graph.
TEST(Crawl, SkipsMalformedFileButIndexesTheRest) {
  const cartograph::Graph graph = cartograph::index_directory(crawl_dir());

  EXPECT_FALSE(has_node(graph, "wont_index", cartograph::NodeKind::Function));

  std::set<std::string> skipped;  // basename -> reason
  std::string reason;
  for (const cartograph::SkippedFile& s : graph.skipped_files()) {
    if (std::filesystem::path(s.path).filename() == "broken.c") reason = s.reason;
  }
  EXPECT_EQ(reason, "syntax error");

  // The good files were still indexed despite the bad one.
  EXPECT_TRUE(has_node(graph, "fa", cartograph::NodeKind::Function));
  EXPECT_TRUE(has_node(graph, "fb", cartograph::NodeKind::Function));
}

// Every File node carries a nonzero content hash, and the hash is deterministic
// across runs — the property incremental reindex (0016) will rely on.
TEST(Crawl, StoresDeterministicContentHashPerFile) {
  const cartograph::Graph a = cartograph::index_directory(crawl_dir());
  const cartograph::Graph b = cartograph::index_directory(crawl_dir());

  std::set<std::uint64_t> hashes;
  std::size_t files = 0;
  for (cartograph::NodeId id = 0; id < a.size(); ++id) {
    const cartograph::Node& node = a.node(id);
    if (node.kind != cartograph::NodeKind::File) continue;
    ++files;
    EXPECT_NE(node.hash, 0u) << node.file << " has no content hash";
    hashes.insert(node.hash);
  }
  EXPECT_GT(files, 0u);
  EXPECT_EQ(hashes.size(), files) << "distinct file contents should hash distinctly";

  // Same inputs, same hashes: a File node's hash matches across the two indexes.
  for (cartograph::NodeId id = 0; id < a.size() && id < b.size(); ++id) {
    if (a.node(id).kind != cartograph::NodeKind::File) continue;
    EXPECT_EQ(a.node(id).hash, b.node(id).hash);
  }
}
