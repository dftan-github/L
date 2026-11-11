/* Layer 1 C runtime: console / basic I/O + C-string helpers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Read one line from stdin into a freshly malloc'd C string (newline stripped).
 * On EOF with no bytes read, returns an empty string (still malloc'd). */
char* l_read_line(void) {
  size_t cap = 128;
  size_t len = 0;
  char* buf = (char*)malloc(cap);
  if (buf == NULL) {
    return NULL;
  }
  buf[0] = '\0';

  for (;;) {
    if (fgets(buf + len, (int)(cap - len), stdin) == NULL) {
      /* EOF or error: keep whatever we have (possibly empty). */
      return buf;
    }
    len += strlen(buf + len);
    if (len > 0 && buf[len - 1] == '\n') {
      buf[len - 1] = '\0';
      --len;
      /* Text-mode stdin usually folds CRLF → LF; strip a leftover CR just in case. */
      if (len > 0 && buf[len - 1] == '\r') {
        buf[len - 1] = '\0';
      }
      return buf;
    }
    /* Line longer than buffer — grow and continue. */
    size_t ncap = cap * 2;
    char* nbuf = (char*)realloc(buf, ncap);
    if (nbuf == NULL) {
      free(buf);
      return NULL;
    }
    buf = nbuf;
    cap = ncap;
  }
}

/* Allocate and return a new null-terminated concatenation of s1 and s2. */
void* l_str_concat(const char* s1, const char* s2) {
  if (s1 == NULL) {
    s1 = "";
  }
  if (s2 == NULL) {
    s2 = "";
  }
  const size_t n1 = strlen(s1);
  const size_t n2 = strlen(s2);
  char* out = (char*)malloc(n1 + n2 + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, s1, n1);
  memcpy(out + n1, s2, n2);
  out[n1 + n2] = '\0';
  return out;
}

void l_panic(const char* message) {
  if (message != NULL) {
    fputs(message, stderr);
    fputc('\n', stderr);
  }
  abort();
}
