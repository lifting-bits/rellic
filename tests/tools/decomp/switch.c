unsigned a = 12;
int main(void) {
  int b = 0;
  switch (a) {
    case 1:
      b = 1;
      break;

    case 12:
      b = 21;
      break;

    case 123:
      b = 321;
      break;

    default:
      b = 255;
      break;
  }
  return b;
}