#include <string.h>

struct _pair {
  int first;
  int second;
};

struct _person {
  const char *name;
  char age;
};

struct _record {
  int a;
  struct _pair b;
  struct _person c;
};

struct _record r1 = {};
long long a1[256] = {};

int main(void) {
  if (r1.b.first != 0)
    return 1;

  if (a1[42] != 0)
    return 2;

  return 0;
}