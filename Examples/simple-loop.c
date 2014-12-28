#include <stdio.h>

int main(int argc, char *argv[]) {
  for (unsigned i = 0; i < argc; ++i)
    printf("%d\n", i);
  return 0;
}

