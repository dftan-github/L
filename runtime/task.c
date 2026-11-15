/* Minimal coroutine task + single-threaded executor (Layer 1).
 *
 * A task wraps one coroutine frame produced by `async fun`. Codegen lowers an
 * async function to `ptr (ptr)` — the argument is the owning LTask*, and each
 * call resumes the coroutine until it either suspends (another await) or
 * completes. `l_task_run` drives tasks to completion in submission order, which
 * is all a minimal executor needs (no preemption, no timers).
 */
#include <stddef.h>
#include <stdint.h>

void* l_alloc(size_t size);
void l_free(void* ptr);

typedef struct LTask {
    /* 0 = created, 1 = suspended (pending resume), 2 = completed. */
    int status;
    /* Frame pointer returned by the LLVM coroutine intrinsics. */
    void* frame;
    /* Pointer to the coroutine's result slot (owned by the frame). */
    void* result;
    /* Resume function: `void (*)(void* frame, int action)`. */
    void* resume_fn;
    /* Next task in the executor queue. */
    struct LTask* next;
} LTask;

/* Queue of runnable tasks; the `await` lowering re-enqueues waiters. */
static LTask* g_head = NULL;
static LTask* g_tail = NULL;

void* l_task_new(void* resume_fn, void* frame) {
    LTask* t = (LTask*)l_alloc(sizeof(LTask));
    if (!t) {
        return NULL;
    }
    t->status = 0;
    t->frame = frame;
    t->result = NULL;
    t->resume_fn = resume_fn;
    t->next = NULL;
    return t;
}

void l_task_set_result(void* task, void* slot) {
    LTask* t = (LTask*)task;
    if (t) {
        t->result = slot;
    }
}

void* l_task_result(void* task) {
    LTask* t = (LTask*)task;
    return t ? t->result : NULL;
}

int l_task_status(void* task) {
    LTask* t = (LTask*)task;
    return t ? t->status : -1;
}

void l_task_set_frame(void* task, void* frame) {
    LTask* t = (LTask*)task;
    if (t) {
        t->frame = frame;
    }
}

/* The coroutine state machine of an `async fun` is resumed through this
 * function pointer: `void (*)(void* frame)`. */
void l_task_set_resume(void* task, void* resume_fn) {
    LTask* t = (LTask*)task;
    if (t) {
        t->resume_fn = resume_fn;
    }
}

void* l_task_frame(void* task) {
    LTask* t = (LTask*)task;
    return t ? t->frame : NULL;
}

/* Append to the run queue (FIFO). A task may be enqueued more than once only
 * after it was dequeued, so the executor never runs a frame concurrently. */
void l_task_enqueue(void* task) {
    LTask* t = (LTask*)task;
    if (!t) {
        return;
    }
    t->next = NULL;
    if (g_tail) {
        g_tail->next = t;
    } else {
        g_head = t;
    }
    g_tail = t;
}

/* Resume a coroutine frame through its resume function. */
typedef void (*LResumeFn)(void* frame, int action);

void l_task_resume(void* task) {
    LTask* t = (LTask*)task;
    if (!t || !t->frame) {
        return;
    }
    if (t->resume_fn) {
        ((LResumeFn)t->resume_fn)(t->frame, 0);
        return;
    }
    /* Switch ABI frame layout: { ptr resume_fn, ptr destroy_fn, i32 index }. */
    LResumeFn fn = *(LResumeFn*)t->frame;
    fn(t->frame, 0);
}

void l_task_complete(void* task) {
    LTask* t = (LTask*)task;
    if (t) {
        t->status = 2;
    }
}

void l_task_suspend(void* task) {
    LTask* t = (LTask*)task;
    if (t) {
        t->status = 1;
    }
}

/* Run every queued task to completion. Returns the number of resumes performed. */
int l_executor_run_all(void) {
    int resumes = 0;
    while (g_head) {
        LTask* t = g_head;
        g_head = t->next;
        if (!g_head) {
            g_tail = NULL;
        }
        t->next = NULL;
        l_task_resume(t);
        ++resumes;
    }
    return resumes;
}

int l_executor_pending(void) {
    int n = 0;
    for (LTask* t = g_head; t; t = t->next) {
        ++n;
    }
    return n;
}

/* Drive the queue until `task` completes, then hand back its result slot.
 * Used by `await`: the waiter blocks (single-threaded MVP) while the executor
 * makes progress on every runnable task, including the one being awaited. */
void* l_task_await(void* task) {
    LTask* t = (LTask*)task;
    if (!t) {
        return NULL;
    }
    while (t->status != 2 && g_head) {
        l_executor_run_all();
    }
    return t->result;
}

/* Typed result accessors: the payload is stored through a frame-local slot, so
 * the caller reads it back with the type it declared for the async function. */
int32_t l_task_result_i32(void* task) {
    LTask* t = (LTask*)task;
    if (!t || !t->result) {
        return 0;
    }
    return *(int32_t*)t->result;
}

int64_t l_task_result_i64(void* task) {
    LTask* t = (LTask*)task;
    if (!t || !t->result) {
        return 0;
    }
    return *(int64_t*)t->result;
}

double l_task_result_f64(void* task) {
    LTask* t = (LTask*)task;
    if (!t || !t->result) {
        return 0.0;
    }
    return *(double*)t->result;
}
