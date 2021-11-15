struct A1 {
  int a;
};

struct A2 {
  int a;
};

struct B : public A1, public A2 {
  int b;
};

void test(B b) {}