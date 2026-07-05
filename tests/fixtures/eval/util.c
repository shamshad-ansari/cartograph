// Shared helpers. init here is file-local and unrelated to cache.c's init.
static int init(void) {
  return 1;
}

int util_hash(int key) {
  return init() + key;
}
