extern int atoi(const char *);

int main(void) {
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