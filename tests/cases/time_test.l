fun main() -> i32 {
    start := time_now_ms()

    // Sleep ~100 ms
    sleep(100)

    end := time_now_ms()
    diff := end - start

    // i64 prints directly (String + i64 concat not yet supported).
    print(start)
    print(diff)

    if diff >= 100 && diff < 500 {
        print("Sleep works!")
    } else {
        print("Time anomaly!")
    }
    return 0
}
