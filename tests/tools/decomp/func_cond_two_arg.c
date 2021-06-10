unsigned f(int a, int b) { return a < b; }
int x = 0;
int y = 1;
int main(void) {
  if (f(x, y)) {
    return 1;
  }
  return 0;
}