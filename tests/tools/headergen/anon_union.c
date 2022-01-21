struct foo {
    union {
        char c;
    };
};

void test(struct foo f) {}