// Caller with no local `provide`; resolves to the external definition in
// extern_def.c.
int uses_extern(void) {
  return provide();
}
