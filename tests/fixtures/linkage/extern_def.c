// External definition of `provide`, called from extern_call.c. With no local
// static shadowing it, the cross-file caller resolves here.
int provide(void) {
  return 0;
}
