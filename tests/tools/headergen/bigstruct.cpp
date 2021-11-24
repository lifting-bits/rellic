struct alignas(1ull << 29) big_foo_t {
  int x[1ull << 32];
};

void test(big_foo_t o) {}