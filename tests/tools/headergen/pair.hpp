#include <utility>

class MyClass {
public:
  std::pair<int, int> my_pair;
  std::pair<int, int> MyMethod(std::pair<int, int> pair);
};