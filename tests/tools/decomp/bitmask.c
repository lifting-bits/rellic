unsigned long a = 0xABCD;
int main(void) {
  if (((a & 0x0FF0) >> 4) == 0xBC) {
    return 0;
  } else {
    return 1;
  }
}