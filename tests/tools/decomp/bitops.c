unsigned a = 0xFF;
unsigned b = 7;

int main(void) {
  if (((int)a >> b) ^ 1) {
    return 0;
  } else if ((a >> b) ^ 1) {
    return 1;
  } else {
    return 2;
  }
}