#include <stdio.h>

int main(void)
{
    for (unsigned b = 0; b != 10; ++b) {
        printf("Variable at %d is ", b);
        if (b % 2 == 0)
            printf("even.\n");
        else
            printf("odd.\n");
    }
    
    return 0;
}