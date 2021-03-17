float f = 3.14;
int main(void) {
  double d = 22.0 / 7.0;
  double e = d - f;
  if (e < 0.01) {
    return 0;
  } else {
    return 1;
  }
}