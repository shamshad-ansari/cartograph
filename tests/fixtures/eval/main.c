#include "store.h"

// Entry point. Calls store_open, then logs via log_msg.
int cache_lookup(Store *s, int key);
int util_hash(int key);
void log_msg(const char *m);

int run(void) {
  Store *s = store_open("db");
  log_msg("store_open failed");
  int a = cache_lookup(s, 1);
  int b = util_hash(2);
  store_close(s);
  return a + b;
}

int main(void) {
  return run();
}
