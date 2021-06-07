#include <stdio.h>
#include <string.h>

int main() {
  char str1[20];
  char str2[20];

  // Assigning the value to the string str1
  strcpy(str1, "hello");

  // Assigning the value to the string str2
  strcpy(str2, "helLO WORLD");

  // This will compare the first 3 characters
  if (strncmp(str1, str2, 3) > 0) {
    printf(
        "ASCII value of first unmatched character of str1 is greater than "
        "str2");
  } else if (strncmp(str1, str2, 3) < 0) {
    printf(
        "ASCII value of first unmatched character of str1 is less than str2");
  } else {
    printf("Both the strings str1 and str2 are equal");
  }

  return 0;
}