int printf(const char*, ...);

int main() {
  int i = 0;
  start:
  i++;
  switch(i) {
    case 1: printf("%d\n", i); goto start; break;
    case 2: printf("%d\n", i); goto start; break;
    case 3: printf("%d\n", i); break;
  }
}