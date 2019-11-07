#include <stdio.h>

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

struct _record r1 = {14, {33, 42}, {"Bob", 66}};

int main(void) {
  printf("Name: %s", r1.c.name);
  return r1.b.second;
}