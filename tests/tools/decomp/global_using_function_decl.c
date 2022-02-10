void some_func(int arg);

void (*afunc_pointer)(int) = &some_func;

void some_func(int arg) {}

int main(void) { afunc_pointer(0); }