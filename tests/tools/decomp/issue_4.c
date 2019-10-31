unsigned int foo(unsigned int a, unsigned int b) {
	unsigned int sum = 0;
	for (unsigned int i = 0; i != 42; i++) {
		sum += a;
		sum %= b;
	}
	return sum;
}

int main() {
	return foo(1, 200);
}