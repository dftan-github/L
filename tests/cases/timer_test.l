fun main() -> i32 {
    print("Start")
    start := time_now_ms()

    // Delayed callback (trailing zero-arg block; may capture `start`).
    delay(100) {
        end := time_now_ms()
        print("Timer fired after " + (end - start) + " ms")
        print("Timer callback executed!")
    }

    print("End of main")
    // Keep main alive so the background thread can finish.
    sleep(200)
    return 0
}
