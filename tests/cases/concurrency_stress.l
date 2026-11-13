i32 global_counter! = 0

fun worker() {
    for i in 0..10000 {
        sync {
            global_counter! = global_counter + 1
        }
    }
}

fun main() -> i32 {
    l_reset_lock_counts()

    // Launch 10 workers asynchronously, keep handles for join.
    h0 := spawn { worker() }
    h1 := spawn { worker() }
    h2 := spawn { worker() }
    h3 := spawn { worker() }
    h4 := spawn { worker() }
    h5 := spawn { worker() }
    h6 := spawn { worker() }
    h7 := spawn { worker() }
    h8 := spawn { worker() }
    h9 := spawn { worker() }

    l_thread_join(h0)
    l_thread_join(h1)
    l_thread_join(h2)
    l_thread_join(h3)
    l_thread_join(h4)
    l_thread_join(h5)
    l_thread_join(h6)
    l_thread_join(h7)
    l_thread_join(h8)
    l_thread_join(h9)

    print(global_counter)
    print(l_get_lock_count())
    return 0
}
