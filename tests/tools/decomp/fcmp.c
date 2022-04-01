float atof(const char*);
int printf(const char*, ...);

int main() {
    float NaN = 0.0f / 0.0f;
    float x = atof("3");
    float y = atof("2");
    float z = -x;

    int a = __builtin_isgreater(z, NaN);
    int b = __builtin_isgreaterequal(z, NaN);

    int c = __builtin_isgreater(x, y);
    int d = __builtin_isgreaterequal(x, y);

    int e = __builtin_isunordered(x, NaN);
    int f = __builtin_isunordered(x, y);

    printf("%d %d %d %d %d %d\n", a, b, c, d, e, f);
}