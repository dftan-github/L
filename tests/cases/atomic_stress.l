i32 global_atomic_counter! = 0

fun worker_atomic() {
    for i in 0..10000 {
        // Lock-free: no sync — C atomics are the synchronization.
        l_atomic_add_i32(&global_atomic_counter, 1)
    }
}

fun main() -> i32 {
    for i in 0..10 {
        spawn { worker_atomic() }
    }
    l_sleep_ms(1000)
    print(global_atomic_counter)
    return 0
}
