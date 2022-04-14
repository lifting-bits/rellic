int printf(const char*, ...);

int main() {
  int i = 0;
  start:
  i++;
  if(i == 1) {
    printf("1\n");
    goto start;
  } else if(i == 2) {
    printf("2\n");
    goto start;
  } else if(i == 3) {
    printf("3\n");;
  }
}