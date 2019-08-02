int a[2] = {0, 42};

int main(void) {
  int b = a[0];
  a[0] = a[1];
  a[1] = b;  
  return a[0];
}