/* Layer 1 C runtime: OS threads + mutex (Win32 / pthreads). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

typedef void (*LThreadFn)(void*);

typedef struct LMutex {
#ifdef _WIN32
  CRITICAL_SECTION cs;
#else
  pthread_mutex_t mu;
#endif
} LMutex;

typedef struct LSpawnThunk {
  LThreadFn fn;
  void* arg;
} LSpawnThunk;

#ifdef _WIN32
typedef struct LThreadHandle {
  HANDLE handle;
} LThreadHandle;
#else
typedef struct LThreadHandle {
  pthread_t thread;
} LThreadHandle;
#endif

/* Process-wide lock backing `sync { ... }` across spawn threads. */
static LMutex* g_sync_mutex = NULL;

/* Atomic counters for global-mutex lock/unlock ([race-detection] validation). */
static volatile int64_t g_lock_count = 0;
static volatile int64_t g_unlock_count = 0;

static void atomic_inc64(volatile int64_t* p) {
#ifdef _WIN32
  InterlockedIncrement64((volatile LONGLONG*)p);
#else
  __sync_fetch_and_add(p, (int64_t)1);
#endif
}

static int64_t atomic_load64(volatile int64_t* p) {
#ifdef _WIN32
  return (int64_t)InterlockedCompareExchange64((volatile LONGLONG*)p, 0, 0);
#else
  return __sync_val_compare_and_swap(p, (int64_t)0, (int64_t)0);
#endif
}

static void atomic_store64(volatile int64_t* p, int64_t v) {
#ifdef _WIN32
  InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v);
#else
  (void)__sync_lock_test_and_set(p, v);
#endif
}

#ifdef _WIN32
static INIT_ONCE g_sync_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_sync_mutex(PINIT_ONCE once, PVOID param, PVOID* ctx) {
  (void)once;
  (void)param;
  LMutex* m = (LMutex*)malloc(sizeof(LMutex));
  if (m == NULL) {
    return FALSE;
  }
  InitializeCriticalSection(&m->cs);
  g_sync_mutex = m;
  if (ctx) {
    *ctx = m;
  }
  return TRUE;
}
#else
static pthread_once_t g_sync_once = PTHREAD_ONCE_INIT;

static void init_sync_mutex(void) {
  LMutex* m = (LMutex*)malloc(sizeof(LMutex));
  if (m == NULL) {
    return;
  }
  pthread_mutex_init(&m->mu, NULL);
  g_sync_mutex = m;
}
#endif

void* l_mutex_init(void) {
  LMutex* m = (LMutex*)malloc(sizeof(LMutex));
  if (m == NULL) {
    return NULL;
  }
#ifdef _WIN32
  InitializeCriticalSection(&m->cs);
#else
  pthread_mutex_init(&m->mu, NULL);
#endif
  return m;
}

void l_mutex_lock(void* mutex) {
  LMutex* m = (LMutex*)mutex;
  if (m == NULL) {
    return;
  }
#ifdef _WIN32
  EnterCriticalSection(&m->cs);
#else
  pthread_mutex_lock(&m->mu);
#endif
  /* Count only the language `sync` global mutex (after acquire). */
  if (m == g_sync_mutex) {
    atomic_inc64(&g_lock_count);
  }
}

void l_mutex_unlock(void* mutex) {
  LMutex* m = (LMutex*)mutex;
  if (m == NULL) {
    return;
  }
  if (m == g_sync_mutex) {
    atomic_inc64(&g_unlock_count);
  }
#ifdef _WIN32
  LeaveCriticalSection(&m->cs);
#else
  pthread_mutex_unlock(&m->mu);
#endif
}

void l_mutex_free(void* mutex) {
  LMutex* m = (LMutex*)mutex;
  if (m == NULL) {
    return;
  }
#ifdef _WIN32
  DeleteCriticalSection(&m->cs);
#else
  pthread_mutex_destroy(&m->mu);
#endif
  free(m);
}

/* Shared mutex for language `sync` blocks (do not free). */
void* l_mutex_global(void) {
#ifdef _WIN32
  InitOnceExecuteOnce(&g_sync_once, init_sync_mutex, NULL, NULL);
#else
  pthread_once(&g_sync_once, init_sync_mutex);
#endif
  return g_sync_mutex;
}

int64_t l_get_lock_count(void) {
  return atomic_load64(&g_lock_count);
}

int64_t l_get_unlock_count(void) {
  return atomic_load64(&g_unlock_count);
}

void l_reset_lock_counts(void) {
  atomic_store64(&g_lock_count, 0);
  atomic_store64(&g_unlock_count, 0);
}

#ifdef _WIN32
static DWORD WINAPI win_thread_entry(LPVOID p) {
  LSpawnThunk* t = (LSpawnThunk*)p;
  if (t != NULL && t->fn != NULL) {
    t->fn(t->arg);
  }
  free(t);
  return 0;
}
#else
static void* pthread_entry(void* p) {
  LSpawnThunk* t = (LSpawnThunk*)p;
  if (t != NULL && t->fn != NULL) {
    t->fn(t->arg);
  }
  free(t);
  return NULL;
}
#endif

/* Start OS thread; returns opaque join handle (or NULL on failure). */
void* l_spawn(void* fn_ptr, void* arg_ptr) {
  LSpawnThunk* t = (LSpawnThunk*)malloc(sizeof(LSpawnThunk));
  if (t == NULL) {
    return NULL;
  }
  t->fn = (LThreadFn)fn_ptr;
  t->arg = arg_ptr;

  LThreadHandle* h = (LThreadHandle*)malloc(sizeof(LThreadHandle));
  if (h == NULL) {
    free(t);
    return NULL;
  }

#ifdef _WIN32
  HANDLE th = CreateThread(NULL, 0, win_thread_entry, t, 0, NULL);
  if (th == NULL) {
    free(t);
    free(h);
    return NULL;
  }
  h->handle = th;
#else
  if (pthread_create(&h->thread, NULL, pthread_entry, t) != 0) {
    free(t);
    free(h);
    return NULL;
  }
#endif
  return h;
}

void l_thread_join(void* handle) {
  LThreadHandle* h = (LThreadHandle*)handle;
  if (h == NULL) {
    return;
  }
#ifdef _WIN32
  WaitForSingleObject(h->handle, INFINITE);
  CloseHandle(h->handle);
#else
  pthread_join(h->thread, NULL);
#endif
  free(h);
}

void l_sleep_ms(int32_t ms) {
  if (ms <= 0) {
    return;
  }
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000);
  ts.tv_nsec = (long)((ms % 1000) * 1000000L);
  nanosleep(&ts, NULL);
#endif
}

/* Lock-free atomics for i32 ([atomics]). Returns the new value after add. */
int32_t l_atomic_add_i32(int32_t* ptr, int32_t val) {
  if (ptr == NULL) {
    return 0;
  }
#ifdef _WIN32
  return (int32_t)InterlockedAdd((volatile LONG*)ptr, (LONG)val);
#else
  return (int32_t)__atomic_add_fetch(ptr, val, __ATOMIC_SEQ_CST);
#endif
}

int32_t l_atomic_load_i32(int32_t* ptr) {
  if (ptr == NULL) {
    return 0;
  }
#ifdef _WIN32
  return (int32_t)InterlockedCompareExchange((volatile LONG*)ptr, 0, 0);
#else
  return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
#endif
}

void l_atomic_store_i32(int32_t* ptr, int32_t val) {
  if (ptr == NULL) {
    return;
  }
#ifdef _WIN32
  (void)InterlockedExchange((volatile LONG*)ptr, (LONG)val);
#else
  __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
#endif
}
