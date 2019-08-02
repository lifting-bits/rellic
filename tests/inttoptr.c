#include <stdlib.h>
unsigned long a = 0xDEADBEEF;
int main(void) {
    int *b = (int *)a;
    if (b != NULL) {
        return 42;
    } else {
        return 0;
    }
}