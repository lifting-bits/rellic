extern int printf(const char* f, ...);
extern int atoi(const char* s);

struct foo {
    long long x, y, z, w;
};

long long get_3x(struct foo f) {
    f.x = f.x * 3;
    return f.x;
}

int main() {
    struct foo f = {atoi("1"), atoi("2"), atoi("3"), atoi("4")};
    long long x = get_3x(f);
    printf("%lld %lld\n", f.x, x);
}