#if defined(__x86_64__) || defined(__i386__) || defined(_M_X86) || \
    defined(__arm__)
typedef unsigned uint128_t __attribute__((mode(TI)));
typedef int int128_t __attribute__((mode(TI)));
#elif defined(__aarch64__)
typedef __uint128_t uint128_t;
typedef __int128_t int128_t;
#elif defined(__sparc__)
typedef __uint128_t uint128_t;
typedef __int128_t int128_t;
#elif !__is_identifier(_ExtInt)
typedef unsigned _ExtInt(128) uint128_t;
typedef signed _ExtInt(128) int128_t;
#else
#error "Unable to identify u/int128 type."
#endif

unsigned long long x = 0xDEADBEEF;

int main(void) {
  uint128_t a = x;
  x = a;
  return x & 0xFF;
}