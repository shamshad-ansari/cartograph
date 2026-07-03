// The sibling of static_a.c. entry_b must resolve to this file's static impl.
static int impl(void) {
  return 2;
}

int entry_b(void) {
  return impl();
}
