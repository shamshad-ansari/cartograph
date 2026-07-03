// Two external definitions of `clash` with differing signatures across
// conflict_a.c and conflict_b.c — a real C link error. Cartograph links a call
// to all matches and flags the conflict rather than guessing (ADR-0005).
int clash(int x) {
  return x;
}
