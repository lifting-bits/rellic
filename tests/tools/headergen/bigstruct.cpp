struct big_foo_t {
  // Reduced from 1ull << 32 (16GB) to avoid LLVM 20 "huge byval" crash
  // See: https://github.com/llvm/llvm-project/issues/115655
  int x[1 << 20];  // ~4MB - still a large struct
};

void test(big_foo_t o) {}