class A {
 public:
  int valA;
  char a;
};
class B : public A {
 public:
  char b;
};
class C : public B {
 public:
  char c;
};

void testA(A* a) {}
void testB(B* b) {}
void testC(C* c) {}