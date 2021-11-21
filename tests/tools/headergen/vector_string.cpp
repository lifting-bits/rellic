#include <string>
#include <vector>

struct foo {
  std::vector<std::string> v;
  bool b;
};

struct bar {
  foo f;
  int i;
};

void test(bar b) {}