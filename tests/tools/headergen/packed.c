struct __attribute__((packed)) packed {
    char c;
    short s;
    int i;
    long int l;
};

void test(struct packed arg) { }