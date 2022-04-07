int puts(const char*);

int main() {
  int i = 0;
  start:
  i++;
  if(i == 1) {
    puts("1\n");
    goto start;
  } else if(i == 2) {
    puts("2\n");
    goto start;
  } else if(i == 3) {
    puts("3\n");;
  }
}