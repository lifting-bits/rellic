#include <stdio.h>

template <typename T>
T sum(T x, T y) {
  return x + y;
}

template <typename T, typename... Ts>
T sum(T x, Ts... y) {
  return x + sum(y...);
}

int main(void) {
  printf("%d\n", sum(1, 2, 3, 4, 5));
  return 0;
}