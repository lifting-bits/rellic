int printf(const char*, ...);

int main() {
  int i = 0;
  start:
  i++;
  switch(i) {
    case 1: printf("1\n"); goto start; break;
    case 2: printf("2\n"); goto start; break;
    case 3: printf("3\n"); break;
  }
}