// A straight call chain top -> mid -> leaf. Changing leaf transitively affects
// both mid (a direct caller) and top (an indirect one, two hops out).
int leaf(int x) {
  return x + 1;
}

int mid(int x) {
  return leaf(x) * 2;
}

int top(int x) {
  return mid(x) - 1;
}
