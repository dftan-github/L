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

void l_print_i64(int64_t val) {
  printf("%lld\n", (long long)val);
  fflush(stdout);
}

void l_print_f64(double val) {
  /* %g keeps short values like 31.4 while printing fuller precision (e.g. 3.14159). */
  printf("%g\n", val);
  fflush(stdout);
}

void l_print_bool(int32_t val) {
  fputs(val ? "true\n" : "false\n", stdout);
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

/* Allocate a decimal representation of `val` (malloc'd; caller frees / owns). */
void* l_int_to_str(int32_t val) {
  char buf[16];
  const int n = snprintf(buf, sizeof(buf), "%d", (int)val);
  if (n < 0) {
    return NULL;
  }
  char* out = (char*)malloc((size_t)n + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, buf, (size_t)n + 1);
  return out;
}

void* l_i64_to_str(int64_t val) {
  char buf[32];
  const int n = snprintf(buf, sizeof(buf), "%lld", (long long)val);
  if (n < 0) {
    return NULL;
  }
  char* out = (char*)malloc((size_t)n + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, buf, (size_t)n + 1);
  return out;
}

void* l_f64_to_str(double val) {
  char buf[64];
  const int n = snprintf(buf, sizeof(buf), "%g", val);
  if (n < 0) {
    return NULL;
  }
  char* out = (char*)malloc((size_t)n + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, buf, (size_t)n + 1);
  return out;
}

void* l_bool_to_str(int32_t val) {
  const char* lit = val ? "true" : "false";
  const size_t n = strlen(lit);
  char* out = (char*)malloc(n + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, lit, n + 1);
  return out;
}

/* One-byte C string (e.g. '{'=123) — avoids L string-interpolation of braces. */
void* l_str_from_byte(int32_t c) {
  char* out = (char*)malloc(2);
  if (out == NULL) {
    return NULL;
  }
  out[0] = (char)(unsigned char)(c & 0xff);
  out[1] = '\0';
  return out;
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

/* FNV-1a 64-bit over a C string (NULL treated as empty). */
uint64_t l_hash_string(const char* s) {
  uint64_t h = 14695981039346656037ULL;
  if (s == NULL) {
    return h;
  }
  for (; *s != '\0'; ++s) {
    h ^= (uint64_t)(unsigned char)*s;
    h *= 1099511628211ULL;
  }
  return h;
}

/* Map open-addressing: hash → bucket in [0, capacity). */
int32_t l_hash_to_bucket(uint64_t hash, int32_t capacity) {
  if (capacity <= 0) {
    return 0;
  }
  return (int32_t)(hash % (uint64_t)capacity);
}

/* 1 if C strings are equal, 0 otherwise (NULL-safe). */
int32_t l_str_eq(const char* a, const char* b) {
  if (a == b) {
    return 1;
  }
  if (a == NULL || b == NULL) {
    return 0;
  }
  return strcmp(a, b) == 0 ? 1 : 0;
}

int32_t l_str_len(const char* s) {
  if (s == NULL) {
    return 0;
  }
  return (int32_t)strlen(s);
}

int32_t l_str_char_at(const char* s, int32_t i) {
  if (s == NULL || i < 0) {
    return 0;
  }
  const size_t n = strlen(s);
  if ((size_t)i >= n) {
    return 0;
  }
  return (int32_t)(unsigned char)s[i];
}

char* l_str_substr(const char* s, int32_t start, int32_t len) {
  if (s == NULL) {
    s = "";
  }
  const size_t n = strlen(s);
  if (start < 0) {
    start = 0;
  }
  if ((size_t)start > n) {
    start = (int32_t)n;
  }
  if (len < 0) {
    len = 0;
  }
  if ((size_t)start + (size_t)len > n) {
    len = (int32_t)(n - (size_t)start);
  }
  char* out = (char*)malloc((size_t)len + 1);
  if (out == NULL) {
    return NULL;
  }
  if (len > 0) {
    memcpy(out, s + start, (size_t)len);
  }
  out[len] = '\0';
  return out;
}

int32_t l_str_to_i32(const char* s) {
  if (s == NULL) {
    return 0;
  }
  return (int32_t)atoi(s);
}

int32_t l_ptr_is_null(void* p) {
  return p == NULL ? 1 : 0;
}

void* l_file_open(const char* path, const char* mode) {
  if (path == NULL || mode == NULL) {
    return NULL;
  }
  return (void*)fopen(path, mode);
}

int32_t l_file_read(void* file, void* buffer, int32_t size) {
  if (file == NULL || buffer == NULL || size <= 0) {
    return -1;
  }
  size_t n = fread(buffer, 1, (size_t)size, (FILE*)file);
  return (int32_t)n;
}

int32_t l_file_write(void* file, const void* buffer, int32_t size) {
  if (file == NULL || buffer == NULL || size < 0) {
    return -1;
  }
  if (size == 0) {
    return 0;
  }
  size_t n = fwrite(buffer, 1, (size_t)size, (FILE*)file);
  return (int32_t)n;
}

void l_file_close(void* file) {
  if (file != NULL) {
    fclose((FILE*)file);
  }
}

int32_t l_file_size(void* file) {
  if (file == NULL) {
    return -1;
  }
  FILE* f = (FILE*)file;
  long cur = ftell(f);
  if (cur < 0) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    return -1;
  }
  long sz = ftell(f);
  if (fseek(f, cur, SEEK_SET) != 0) {
    return -1;
  }
  if (sz < 0 || sz > 0x7FFFFFFF) {
    return -1;
  }
  return (int32_t)sz;
}
