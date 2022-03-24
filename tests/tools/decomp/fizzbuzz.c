int printf(const char*, ...);

int main() {
    for(int i = 0; i < 30; ++i) {
        if(i % 3 == 0 && i % 5 == 0) {
            printf("fizzbuzz\n");
        } else if(i % 3 == 0) {
            printf("fizz\n");
        } else if(i % 5 == 0) {
            printf("buzz\n");
        } else {
            printf("%d\n", i);
        }
    }
}