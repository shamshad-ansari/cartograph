#include "shapes.h"

/* References struct Point in a parameter. */
int point_x(struct Point p) {
  return p.x;
}

/* References struct Point (return type + local) and enum Color (parameter). */
struct Point origin(enum Color c) {
  struct Point p;
  p.x = 0;
  p.y = 0;
  return p;
}

/* References the Celsius typedef (return type + local). */
Celsius freezing(void) {
  Celsius c = 0;
  return c;
}

/* References no user-defined type. */
int add(int a, int b) {
  return a + b;
}
