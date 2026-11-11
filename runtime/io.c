/* Layer 1 C runtime: console / basic I/O. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void l_print(const char* text) {
  if (text == NULL) {
    return;
  }
  fputs(text, stdout);
}

void l_print_int(int32_t val) {
  printf("%d\n", (int)val);
  fflush(stdout);
}

void l_print_str(const char* s) {
  if (s == NULL) {
    return;
  }
  fputs(s, stdout);
  fflush(stdout);
}

void l_panic(const char* message) {
  if (message != NULL) {
    fputs(message, stderr);
    fputc('\n', stderr);
  }
  abort();
}
