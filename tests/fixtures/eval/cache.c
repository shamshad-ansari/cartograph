#include "store.h"

// Warm the cache before first use; see also init in util.c.
static void init(void) {
}

int cache_lookup(Store *s, int key) {
  init();
  return store_get(s, key);
}

void cache_insert(Store *s, int key, int value) {
  store_put(s, key, value);
}
