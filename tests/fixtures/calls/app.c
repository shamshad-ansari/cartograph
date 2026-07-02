int helper(int x) {
  return x + 1;
}

int run(int n) {
  int t = helper(n);
  if (n > 0) {
    t += compute(n);
  }
  return t;
}

int main(void) {
  return run(3) + helper(0);
}
