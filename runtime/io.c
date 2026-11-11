/* Layer 1 C runtime: console / basic I/O. */
#include <stdio.h>
#include <stdlib.h>

void l_print(const char* text) {
  if (text == NULL) {
    return;
  }
  fputs(text, stdout);
}

void l_panic(const char* message) {
  if (message != NULL) {
    fputs(message, stderr);
    fputc('\n', stderr);
  }
  abort();
}
