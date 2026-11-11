/* Layer 1 C runtime: heap allocation. Wrappers around malloc/realloc/free. */
#include <stddef.h>
#include <stdlib.h>

void* l_alloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  return malloc(size);
}

void* l_realloc(void* ptr, size_t new_size) {
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new_size);
}

void l_free(void* ptr) {
  free(ptr);
}
