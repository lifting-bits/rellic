extern int atoi(const char *);

int foo() {
  int a = 0;
  return a;
}

int bar() {
  int a = 1;
  return a;
}

int main() {
  int argc = 0;
  char **argv = 0;
  int ret = 0;
  if (1 < argc) {
    int argc = atoi(argv[1]);
    if (argc > 10) {
      int argc = 99;
      ret = argc;
    }
  } else {
    int argv = 1;
    ret = argv;
  }
  return ret;
}