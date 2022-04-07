int puts(const char*);

int main() {
  int i = 0;
  start:
  i++;
  switch(i) {
    case 1: puts("1\n"); goto start; break;
    case 2: puts("2\n"); goto start; break;
    case 3: puts("3\n"); break;
  }
}