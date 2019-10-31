int add(int a, int b) { return a + b; }

int sub(int a, int b) { return a - b; }

int x = 0;

int main(void) {
  int (*func)(int, int);
  if (x) {
    func = add;
  } else {
    func = sub;
  }
  return func(2, 2);
}