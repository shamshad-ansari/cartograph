#include "cartograph/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace cartograph {

unsigned default_thread_count() {
  const unsigned n = std::thread::hardware_concurrency();
  return n == 0 ? 1u : n;
}

void parallel_for(std::size_t count, unsigned threads,
                  const std::function<void(std::size_t)>& body) {
  if (count == 0) return;

  // One worker, or a range too small to be worth a thread, runs inline: no
  // pool, no join overhead — the single-threaded reference path.
  const unsigned workers =
      static_cast<unsigned>(std::min<std::size_t>(std::max(threads, 1u), count));
  if (workers <= 1) {
    for (std::size_t i = 0; i < count; ++i) body(i);
    return;
  }

  // The queue is a single monotonic cursor the workers fetch-and-increment;
  // each claims one index at a time, so a slow file doesn't starve the others.
  std::atomic<std::size_t> cursor{0};
  const auto run = [&] {
    for (;;) {
      const std::size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
      if (i >= count) return;
      body(i);
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(workers - 1);
  for (unsigned w = 1; w < workers; ++w) pool.emplace_back(run);
  run();  // the calling thread is a worker too
  for (std::thread& t : pool) t.join();
}

}  // namespace cartograph
