struct __attribute__((packed)) b1 {
    int a:12;
    int b:32;
    int c:4;
};

void test(struct b1 arg) {}