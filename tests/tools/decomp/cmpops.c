__attribute__ ((used))
unsigned int target(unsigned int n) {
    if (n >= 10 && n <= 20) {
        return n * 3 + 4;
    }

    unsigned int x = (signed int) n;

    if (x >= 1 && x <= 9) {
        return x * 2 + 1;
    }

    if (n > 3 || n < 4) {
        return x > 10;
    }

    return n + 10 < x * 10;
}

int main(void) {
    return 0;
}
