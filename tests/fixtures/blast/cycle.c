// Mutual recursion: ping <-> pong. pong also calls leaf (defined in chain.c),
// so both are in leaf's blast radius, and the reverse traversal must follow the
// ping <-> pong cycle without looping forever.
int ping(int n);
int leaf(int x);

int pong(int n) {
  return ping(n) + leaf(n);
}

int ping(int n) {
  return pong(n);
}
