typedef struct {
  int a;
  int b;
  union {
    int x;
    char y;
  };
  union {
    int s;
    int t;
  };
} foo_t;

int bar(foo_t arg) {
  return arg.s;
}

extern int printf(const char* f, ...);
extern int atoi(const char* s);

int main() {
    foo_t f;
    f.t = 3;
    printf("%d\n", bar(f));
}