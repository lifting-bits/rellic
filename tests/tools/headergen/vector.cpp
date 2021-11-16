#include <vector>

int main(int argc, char* argv[]) {
  std::vector<char*> args(argc);
  return args.size();
}