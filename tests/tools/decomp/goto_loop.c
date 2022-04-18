int printf(const char*, ...);

int main() {
  int i = 0;
  start:
  i++;
  if(i == 1) {
    printf("%d\n", i);
    goto start;
  } else if(i == 2) {
    printf("%d\n", i);
    goto start;
  } else if(i == 3) {
    printf("%d\n", i);;
  }
}