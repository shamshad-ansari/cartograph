#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cartograph {

class Tree;

// A raw structural fact produced by matching tree-sitter query patterns against
// a parsed source file, before it is interned into the graph.
//
// Extraction is kept deliberately separate from graph construction and query
// resolution: supporting a new language or a new relationship is then mostly a
// matter of new .scm patterns plus a fact type, not changes to graph or query
// code.
struct DefinitionFact {
  std::string name;
  std::uint32_t line;  // 1-based line of the function's name
  bool is_static;      // has a `static` storage class -> internal linkage
};

// A function prototype: a declaration without a body, distinct from a
// DefinitionFact. Linkage is not tracked — a prototype declares an
// external-linkage function unless a matching static definition says otherwise,
// which the indexer resolves when it links the declaration to its definition.
struct DeclarationFact {
  std::string name;
  std::uint32_t line;  // 1-based line of the prototype's name
};

// A call site: the enclosing function's name (`caller`) and the still-
// unresolved name it calls (`callee`). Resolution to definition node(s) happens
// later, in the indexer, once every file's definitions are known.
struct CallFact {
  std::string caller;
  std::string callee;
  std::uint32_t line;  // 1-based line of the call site
};

// An `#include` directive: the path token as written (delimiters stripped) and
// whether it used the angle-bracket system form. Resolution of `target` to an
// indexed File node happens later, in the indexer, once every file is known.
struct IncludeFact {
  std::string target;  // path as written, without quotes/brackets: util.h, stdio.h
  bool is_system;      // `<...>` form -> a system/external header, not resolved
  std::uint32_t line;  // 1-based line of the #include directive
};

// The category of a user-defined type declaration. Kept as an extractor-local
// enum (rather than reaching for graph.hpp's NodeKind) so extraction stays
// decoupled from graph construction; the indexer maps it to a NodeKind.
enum class TypeCategory {
  Struct,
  Union,
  Enum,
  Typedef,
};

// A user-defined type declaration: the tag of an aggregate definition (a struct,
// union, or enum with a body) or a typedef alias, plus which of those it is and
// where it was found.
struct TypeFact {
  std::string name;
  std::uint32_t line;  // 1-based line of the type's name
  TypeCategory category;
};

// A site where a function references a type: the enclosing function's name and
// the still-unresolved type name it mentions (in a return type, parameter, or
// local declaration). Resolution to a type node happens later, in the indexer,
// once every file's type declarations are known.
struct TypeUseFact {
  std::string function;  // enclosing function's name
  std::string type;      // the referenced type name, unresolved
  std::uint32_t line;    // 1-based line of the reference
};

// Extract function-definition facts from an already-parsed C source `tree`.
// `source` must be the exact buffer the tree was parsed from; it is used to read
// captured identifier text. Returns an empty vector for an empty tree.
std::vector<DefinitionFact> extract_definitions(const Tree& tree,
                                                std::string_view source);

// Extract function-prototype facts from an already-parsed C source `tree` — the
// `int foo(int);` declarations that headers use, as opposed to definitions with
// a body. `source` must be the buffer the tree was parsed from. Returns an empty
// vector for an empty tree.
std::vector<DeclarationFact> extract_declarations(const Tree& tree,
                                                  std::string_view source);

// Extract call-site facts from an already-parsed C source `tree`. Each call is
// attributed to the function_definition that encloses it; calls not inside a
// recognized function definition are dropped. `source` must be the buffer the
// tree was parsed from. Returns an empty vector for an empty tree.
std::vector<CallFact> extract_calls(const Tree& tree, std::string_view source);

// Extract `#include` directive facts from an already-parsed C source `tree`, in
// source order. Both the local `"..."` and system `<...>` forms are returned;
// the `is_system` flag distinguishes them. `source` must be the buffer the tree
// was parsed from. Returns an empty vector for an empty tree.
std::vector<IncludeFact> extract_includes(const Tree& tree,
                                          std::string_view source);

// Extract user-defined type declarations (struct/union/enum definitions and
// typedefs) from an already-parsed C source `tree`. `source` must be the buffer
// the tree was parsed from. Returns an empty vector for an empty tree.
std::vector<TypeFact> extract_types(const Tree& tree, std::string_view source);

// Extract type-reference facts from an already-parsed C source `tree`. Each
// reference is attributed to the function_definition that encloses it;
// references outside a recognized function definition (e.g. a type used in
// another type's declaration) are dropped. `source` must be the buffer the tree
// was parsed from. Returns an empty vector for an empty tree.
std::vector<TypeUseFact> extract_type_uses(const Tree& tree,
                                           std::string_view source);

}  // namespace cartograph
