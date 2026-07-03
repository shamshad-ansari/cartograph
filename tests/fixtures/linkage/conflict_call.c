// Caller of the ambiguous external `clash`. It links to both definitions and
// the resolution is surfaced as a diagnostic.
int conflict_caller(void) {
  return clash(1);
}
