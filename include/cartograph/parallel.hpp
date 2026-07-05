#pragma once

#include <cstddef>
#include <functional>

namespace cartograph {

// The default worker count for the indexing pipeline: the hardware's reported
// concurrency, or 1 when the platform can't report it. Callers pass 0 for
// "auto" and the pipeline resolves it through here.
unsigned default_thread_count();

// Run `body(i)` for every i in [0, count) across a work-queue thread pool of
// `threads` workers, blocking until all indices are done. Workers pull indices
// from a shared atomic cursor — the queue — so the load balances even when the
// per-index cost is uneven (some files parse far slower than others).
//
// The contract mirrors ADR-0006's share-nothing shape: `body` invocations must
// be independent, touching only per-index state (e.g. results[i]); given that,
// this function adds no synchronization of its own beyond the cursor and needs
// none. `threads <= 1` (or a trivially small range) runs inline on the calling
// thread, so single-threaded is a genuine no-pool path, not a pool of one.
void parallel_for(std::size_t count, unsigned threads,
                  const std::function<void(std::size_t)>& body);

}  // namespace cartograph
