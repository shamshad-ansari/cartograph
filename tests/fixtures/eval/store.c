#include "store.h"

struct Store {
  int size;
};

Store *store_open(const char *path) {
  (void)path;
  return 0;
}

void store_close(Store *s) {
  (void)s;
}

int store_get(Store *s, int key) {
  (void)s;
  return key;
}

void store_put(Store *s, int key, int value) {
  (void)s;
  (void)key;
  (void)value;
}
