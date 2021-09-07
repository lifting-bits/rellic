#include <stdio.h>
typedef int v4si __attribute__((vector_size(16)));

int main() {
  v4si a, b, c;
  a = (v4si){1, 2, 3, 4};
  b = (v4si){};
  c = (v4si){4, 3, 2, 1};
  a = a * b + c;
  int *pA = (int *)&a;
  printf("a=[%d %d %d %d]\n", pA[0], pA[1], pA[2], pA[3]);
  return 0;
}