int shared(void) {
  return 1;
}

int caller_one(void) {
  return shared();
}
