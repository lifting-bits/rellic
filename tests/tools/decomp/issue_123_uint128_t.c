typedef unsigned uint128_t __attribute__((mode(TI)));
typedef int int128_t __attribute__((mode(TI)));

unsigned long long x = 0xDEADBEEF;

int main(void) {
  uint128_t a = x;
  x = a;
  return x & 0xFF;
}