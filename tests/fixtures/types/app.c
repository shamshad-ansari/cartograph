#include "shapes.h"

/* References union Value, in a different file from the type's declaration. */
int as_int(union Value v) {
  return v.i;
}
