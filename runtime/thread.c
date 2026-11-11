/* Layer 1 C runtime: thread / mutex stubs for Phase 1. */
#include <stddef.h>

/* Opaque handles; real Win32/pthreads wiring comes later. */
typedef struct LMutex {
  int unused;
} LMutex;

int l_thread_spawn(void (*fn)(void*), void* arg) {
  (void)fn;
  (void)arg;
  /* Stub: not implemented in Phase 1 scaffolding. */
  return -1;
}

void l_mutex_lock(LMutex* m) {
  (void)m;
}

void l_mutex_unlock(LMutex* m) {
  (void)m;
}
