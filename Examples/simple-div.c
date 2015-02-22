#include <stdio.h>

int main(int argc, char *argv[]) {
  int a = argc % 2 == 0 ? 1 : argc;
  int b = argc % 2 == 0 ? 5 : argc;
  //int b = argc % 2 == 0 ? 5 : 10;
  int c = a/b;
  printf("%d\n", c);
  return 0;
}

