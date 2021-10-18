#include <stdio.h>

int a = 3;

int main(void) {
  {
    int a = 4;
    printf("%d\n", a);
  }
  printf("%d\n", a);
}
