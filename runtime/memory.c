/* Layer 1 C runtime: heap allocation. Wrappers around malloc/free. */
#include <stddef.h>
#include <stdlib.h>

void* l_alloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  return malloc(size);
}

void l_free(void* ptr) {
  free(ptr);
}
