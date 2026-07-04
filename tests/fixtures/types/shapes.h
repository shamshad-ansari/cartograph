#ifndef SHAPES_H
#define SHAPES_H

/* A tagged aggregate: referenced elsewhere as `struct Point`. */
struct Point {
  int x;
  int y;
};

/* An enum, referenced as `enum Color`. */
enum Color {
  RED,
  GREEN,
  BLUE
};

/* A union, referenced as `union Value`. */
union Value {
  int i;
  float f;
};

/* A plain typedef alias, referenced bare as `Celsius`. */
typedef int Celsius;

#endif  /* SHAPES_H */
