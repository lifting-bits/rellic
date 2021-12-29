struct A1 {
  int a;
};

struct A2 {};

struct B : public A1, public A2 {};

void test(B b) {}