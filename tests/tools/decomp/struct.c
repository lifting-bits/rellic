struct _pair {
  int first;
  int second;
};

struct _pair a = {0, 42};

int main(void) {
  if (a.first) {
    return a.first;
  } else {
    return a.second;
  }
}