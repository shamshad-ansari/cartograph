// A straight call chain leaf <- mid <- top for the MCP adapter test.
// Changing leaf transitively affects mid (a direct caller) and top (indirect).
int leaf(int x) {
  return x + 1;
}

int mid(int x) {
  return leaf(x) * 2;
}

int top(int x) {
  return mid(x) - 1;
}
