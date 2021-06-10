unsigned f() { return 1U; }
int x = 0;
int y = 1;
int main(void) {
  if (f()) {
    return 1;
  }
  return 0;
}