#include <stdio.h>
int main(int argc, const char** argv) {
  int i = argc;
  if (i == 1) {  // no args
    putchar(4 + '0');
    i += 1;
    goto n5;
  }
  if (i > 1) {  // args
    putchar(5 + '0');
    goto n7;
  }
n5:
  putchar(6 + '0');
n7:
  putchar(7 + '0');
}
