struct __attribute__((packed)) b1 {
    int a:12;
    int b;
    int c:4;
};

void test(struct b1 arg) {}