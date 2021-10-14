#include <stdio.h>
typedef long long int mytype;
typedef mytype foo;
typedef foo bar;
typedef bar baz __attribute__((vector_size(16)));
typedef baz foobar;

static foo array[4] = {};

int main() {
  foobar a, b, c;
  a = (foobar){1, 2};
  b = (foobar){};
  c = (foobar){4, 3};
  a = a * b + c;
  int *pA = (int *)&a;

  printf("a=[%d %d]\n", pA[0], pA[1]);
  printf("array=[%lld %lld %lld %lld]\n", array[0], array[1], array[2], array[3]);
  return 0;
}