__attribute__ ((used))
unsigned int target(unsigned int n) {
  unsigned int mod = n % 5;
  unsigned int result = 0;

  if (mod == 0) {
    result = (n | 0xbaaad0bf) * (2 ^ n);
  } else if (mod == 1) {
    result = (n & 0xbaaad0bf) * (3 + n);
  } else if (mod == 2) {
    result = (n ^ 0xbaaad0bf) * (4 | n);
  } else if (mod == 3) {
    result = (~n - 0xbaaad0bf) * (5 / n);
  } else {
    result = (n + 0xbaaad0bf) * (6 & n);
  }

  return result;
}

int main(void) {
    return 0;
}
