struct _pair {
  int first;
  int second;
};

struct _pair a = {0, 42};

int main(void) {
  int b = a.first;
  a.first = a.second;
  a.second = b;  
  return a.first;
}