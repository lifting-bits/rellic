#include <assert.h>

unsigned long a = 1;

int main(void) {
    assert(a % 3);
    assert(a % 7);
    assert(a % 15);
    
    return 0;
}