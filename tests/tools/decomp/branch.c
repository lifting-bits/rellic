#include <stdint.h>
#include <stdio.h>

unsigned a = 0;
unsigned c = 1;

int main(void) {
  uintptr_t b = (uintptr_t)&a;
  uintptr_t d = (uintptr_t)&c;

  if (c) {
    printf("Global variable 'a' of value %u is at ", a);
    if (b % 2 == 0)
      printf("even ");
    else
      printf("odd ");
  } else {
    printf("Global variable 'c' of value %u is at ", c);
    if (d % 2 == 0)
      printf("even ");
    else
      printf("odd ");
  }

  printf("address.\n");

  return 0;
}