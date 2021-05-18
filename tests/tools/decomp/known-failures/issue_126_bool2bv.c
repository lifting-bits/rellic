unsigned long a = 0xABCD;
int main(void) {
  if (!((a & 0xF000) ^ 0xA000) & !((a & 0x0F00) ^ 0x0B00)) {
    return 0;
  } else {
    return 1;
  }
}