/* Layer 1 C runtime: heap allocation + bump arena. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int32_t l_alloc_count = 0;

typedef struct LArena {
  unsigned char* base;
  size_t capacity;
  size_t offset;
} LArena;

void* l_alloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  void* p = malloc(size);
  if (p != NULL) {
    ++l_alloc_count;
  }
  return p;
}

void* l_realloc(void* ptr, size_t new_size) {
  if (new_size == 0) {
    if (ptr != NULL) {
      free(ptr);
      --l_alloc_count;
    }
    return NULL;
  }
  /* realloc may move; count stays the same if ptr was already tracked. */
  if (ptr == NULL) {
    void* p = realloc(NULL, new_size);
    if (p != NULL) {
      ++l_alloc_count;
    }
    return p;
  }
  return realloc(ptr, new_size);
}

void l_free(void* ptr) {
  if (ptr != NULL) {
    free(ptr);
    --l_alloc_count;
  }
}

int32_t l_get_alloc_count(void) {
  return l_alloc_count;
}

/* Typed stores/loads for Variant (and similar) through opaque ptr cells. */
void l_store_i32(void* p, int32_t v) {
  if (p != NULL) {
    *(int32_t*)p = v;
  }
}

void l_store_u8(void* p, int32_t index, int32_t v) {
  if (p != NULL && index >= 0) {
    ((unsigned char*)p)[index] = (unsigned char)(v & 0xff);
  }
}

/* Byte-offset accessors: let L code model a small record without pointer
 * arithmetic (raw `ptr + i32` is rejected by the type checker). */
void l_store_i32_at(void* p, int32_t offset, int32_t v) {
  if (p != NULL && offset >= 0) {
    *(int32_t*)((unsigned char*)p + offset) = v;
  }
}

int32_t l_load_i32_at(void* p, int32_t offset) {
  if (p == NULL || offset < 0) {
    return 0;
  }
  return *(const int32_t*)((const unsigned char*)p + offset);
}

void l_store_ptr_at(void* p, int32_t offset, void* v) {
  if (p != NULL && offset >= 0) {
    *(void**)((unsigned char*)p + offset) = v;
  }
}

void* l_load_ptr_at(void* p, int32_t offset) {
  if (p == NULL || offset < 0) {
    return NULL;
  }
  return *(void* const*)((const unsigned char*)p + offset);
}

int32_t l_load_i32(void* p) {
  if (p == NULL) {
    return 0;
  }
  return *(const int32_t*)p;
}

void l_store_f64(void* p, double v) {
  if (p != NULL) {
    *(double*)p = v;
  }
}

double l_load_f64(void* p) {
  if (p == NULL) {
    return 0.0;
  }
  return *(const double*)p;
}

void l_store_ptr(void* p, void* v) {
  if (p != NULL) {
    *(void**)p = v;
  }
}

void* l_load_ptr(void* p) {
  if (p == NULL) {
    return NULL;
  }
  return *(void* const*)p;
}

/* A well-typed null pointer value for L code (L has no `0 as ptr` cast). */
void* l_null_ptr(void) { return NULL; }

/* Bump allocator: one malloc'd slab, freed all at once via l_arena_free. */
void* l_arena_new(size_t size) {
  LArena* a = (LArena*)malloc(sizeof(LArena));
  if (a == NULL) {
    return NULL;
  }
  const size_t cap = size == 0 ? 1 : size;
  a->base = (unsigned char*)malloc(cap);
  if (a->base == NULL) {
    free(a);
    return NULL;
  }
  a->capacity = cap;
  a->offset = 0;
  return a;
}

void* l_arena_alloc(void* arena, size_t size) {
  LArena* a = (LArena*)arena;
  if (a == NULL || a->base == NULL) {
    return NULL;
  }
  /* 8-byte align bump pointer. */
  const size_t align = 8;
  size_t off = (a->offset + (align - 1)) & ~(align - 1);
  if (size > a->capacity || off > a->capacity - size) {
    return NULL;
  }
  void* p = a->base + off;
  a->offset = off + size;
  return p;
}

void l_arena_free(void* arena) {
  LArena* a = (LArena*)arena;
  if (a == NULL) {
    return;
  }
  free(a->base);
  free(a);
}
