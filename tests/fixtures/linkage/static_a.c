// One of two files that each define a *different* `static impl`. Its caller,
// entry_a, must resolve to this file's impl and not to static_b.c's — the case
// naive name-matching gets wrong.
static int impl(void) {
  return 1;
}

int entry_a(void) {
  return impl();
}
