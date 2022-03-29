int printf(const char*, ...);

int i;

void fizzbuzz() {
  int save = i;
  if (i % 3 == 0) {
    i = 4;
    printf("fizz");
  }

  if (i % 5 == 0) {
    printf("buzz");
  }

  if ((i % 3 != 0) && (i % 5 != 0)) {
    printf("%d", i);
  }
  i = save;
}

int main() {
  for (i = 1; i < 16; i++) {
    fizzbuzz();
    printf("\n");
  }
}