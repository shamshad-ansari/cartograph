// External `shade` in another translation unit. It shares a name with the
// static `shade` in local_static.c but has external linkage, so the caller in
// local_static.c must NOT resolve to it.
int shade(void) {
  return 1;
}
