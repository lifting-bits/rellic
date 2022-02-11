struct a {
	int x;
};

struct b {
	struct a subfield;
};

int main(void) {
	struct b a;
}
