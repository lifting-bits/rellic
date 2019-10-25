int a = 0xFF;

int main(void) {
    a = (unsigned int)a >> 7;
    return a ^ 1;
}