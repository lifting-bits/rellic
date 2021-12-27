struct foo;
struct bar;
struct baz;

struct foo {
  bar* x;
};

struct bar {
  foo* x;
  bar* y;
  baz* z;
};

struct baz : foo {
  foo* x;
};

void test(foo a, bar b, baz c) {}