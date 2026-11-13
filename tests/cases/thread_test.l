fun main() -> i32 {
    i32 counter! = 0

    // Background thread mutates shared state under sync; join the handle explicitly.
    h := spawn {
        sync {
            counter! = counter + 1
        }
    }
    l_thread_join(h)

    print(counter)
    return 0
}
