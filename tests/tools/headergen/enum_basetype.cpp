enum myenum : unsigned long long { MYENUM_VAL = 1ull << 56 };
enum myenum2 { MYENUM_VAL2 = 1 };

void test(myenum e, myenum2 f) {}