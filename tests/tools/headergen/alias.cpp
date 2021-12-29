struct A {
  A* a;
};

struct C {
  using foo = A;
};

struct B {
  using foo = C::foo;
  foo a;
};

void test(B b) {}