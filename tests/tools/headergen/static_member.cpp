struct static_members_t {
  int a;
  static int b;
};

int static_members_t::b = 0;

void test(static_members_t arg) {}