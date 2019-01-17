#include <stdio.h>

unsigned a = 0;
unsigned c = 1;

int main(void)
{
    unsigned *b = &a;
    unsigned *d = &c;
    
    if (c) {
        printf("Global variable 'a' of value %u at address %p is ", a, b);
        if (a % 2 == 0)
            printf("even.\n");
        else
            printf("odd.\n");
    } else {
        printf("Global variable 'c' of value %u at address %p is ", c, d);
        if (c % 2 == 0)
            printf("even.\n");
        else
            printf("odd.\n");
    }
    
    return 0;
}