// Negative fixture: `target` is only ever called from inside a function-like
// macro body. Because Cartograph does not run the preprocessor (ADR-0005), the
// macro is not expanded, the `INDIRECT()` call site names the macro rather than
// `target`, and `target` therefore has NO caller. Documented limitation, pinned.
int target(void) {
  return 0;
}

#define INDIRECT() target()

int macro_user(void) {
  return INDIRECT();
}
