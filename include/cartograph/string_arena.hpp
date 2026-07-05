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
//
// An arena is either *owning* (the default — bytes live in `bytes_`, appended by
// `intern`) or *mapped* (constructed by `mapped()` — bytes are viewed in place in
// an externally-owned buffer, typically an mmap'd index file, ADR-0008). A mapped
// arena is read-only: `view` resolves refs against the borrowed bytes, but
// `intern` must not be called on it.
class StringArena {
 public:
  StringArena() = default;

  // A read-only arena whose bytes are `bytes` — memory owned by someone else (an
  // mmap'd region) and kept alive for the arena's lifetime by that owner. Used by
  // a warm load to reference the persisted string bytes without copying them.
  static StringArena mapped(std::string_view bytes) {
    StringArena arena;
    arena.mapped_ = true;
    arena.mapped_bytes_ = bytes;
    return arena;
  }

  StringRef intern(std::string_view s) {
    if (const auto it = index_.find(s); it != index_.end()) return it->second;
    const StringRef ref{static_cast<std::uint32_t>(bytes_.size()),
                        static_cast<std::uint32_t>(s.size())};
    bytes_.append(s);
    index_.emplace(std::string(s), ref);
    return ref;
  }

  std::string_view view(StringRef ref) const {
    return bytes().substr(ref.offset, ref.length);
  }

  // The whole interned buffer, whichever mode the arena is in — the bytes a save
  // writes out, and what `view` slices against.
  std::string_view bytes() const noexcept {
    return mapped_ ? mapped_bytes_ : std::string_view(bytes_);
  }

  // Total interned bytes — the arena's contribution to graph memory, and the
  // hook the dedup regression test reads.
  std::size_t size_bytes() const noexcept { return bytes().size(); }

 private:
  bool mapped_ = false;
  std::string_view mapped_bytes_;  // mapped mode: bytes owned elsewhere
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
