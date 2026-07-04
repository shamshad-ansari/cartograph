/* Deliberately malformed: an unterminated function body. The error-tolerant
   parser yields a tree with an ERROR node, so this file must be skipped and its
   `wont_index` definition must never enter the graph. */
int wont_index(void) {
  return 0;
