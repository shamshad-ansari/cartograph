#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cartograph {

// A reference into a StringArena: an interned string is addressed by its byte
// offset and length, never by a pointer. This keeps the struct-of-arrays graph
// (ADR-0007) position-independent — a prerequisite for mmap persistence — and
// shrinks a node's name/file field from a 32-byte heap `std::string` to 8 bytes.
struct StringRef {
  std::uint32_t offset = 0;
  std::uint32_t length = 0;
};

// An append-only pool of interned strings. `intern` copies a string's bytes into
// one contiguous buffer (deduplicating identical content) and returns a StringRef
// to them; `view` resolves a ref back to a string_view. Because `view` recomputes
// against the current buffer, refs stay valid across the reallocations that
// growth triggers — unlike raw pointers into the buffer.
class StringArena {
 public:
  StringRef intern(std::string_view s) {
    if (const auto it = index_.find(s); it != index_.end()) return it->second;
    const StringRef ref{static_cast<std::uint32_t>(bytes_.size()),
                        static_cast<std::uint32_t>(s.size())};
    bytes_.append(s);
    index_.emplace(std::string(s), ref);
    return ref;
  }

  std::string_view view(StringRef ref) const {
    return std::string_view(bytes_).substr(ref.offset, ref.length);
  }

  // Total interned bytes — the arena's contribution to graph memory, and the
  // hook the dedup regression test reads.
  std::size_t size_bytes() const noexcept { return bytes_.size(); }

 private:
  // Transparent hashing lets `intern` look a string_view up in the dedup index
  // without first allocating a std::string key for it.
  struct TransparentStringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
      return std::hash<std::string_view>{}(s);
    }
  };

  std::string bytes_;
  std::unordered_map<std::string, StringRef, TransparentStringHash,
                     std::equal_to<>>
      index_;
};

}  // namespace cartograph
