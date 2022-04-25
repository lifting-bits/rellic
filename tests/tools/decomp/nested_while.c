int atoi(const char*);
int printf(const char*, ...);

int main() {
    int x = atoi("5");
    if(x > 10) {
        while(x < 20) {
            x = x + 1;
            printf("loop1 x: %d\n", x);
        }
    }
    while(x < 20) {
        x = x + 1;
        printf("loop2 x: %d\n", x);
    }
}