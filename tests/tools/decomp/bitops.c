unsigned a = 0xFF;
unsigned b = 7;

int main(void) {
  int retval = 0;
  if (((int)a >> b) & 1) {
    retval += 1;
  }
  if ((a >> b) ^ 1) {
    retval += 2;
  }
  if ((a << b) || 1) {
    retval += 3;
  }
  return retval;
}