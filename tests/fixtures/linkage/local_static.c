// A local `static` definition of `shade` plus a caller in the same file. The
// call must resolve here (internal linkage), never to the external `shade` in
// local_extern.c.
static int shade(void) {
  return 0;
}

int uses_local(void) {
  return shade();
}
