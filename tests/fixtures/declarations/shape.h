#ifndef SHAPE_H
#define SHAPE_H

// A prototype whose definition lives in shape.c — the declaration should link
// to that definition.
double area(double r);

// A pointer-returning prototype, also defined in shape.c.
char *name(void);

// Declared here but defined nowhere in the indexed set: it stays an unlinked
// declaration.
int perimeter(int sides);

#endif  // SHAPE_H
