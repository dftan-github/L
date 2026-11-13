fun main() -> i32 {
    now := time_now()
    formatted := format_time(now, "%Y-%m-%d %H:%M:%S")
    print("Current time: " + formatted)

    // Epoch 0 → 1969 or 1970 depending on local timezone.
    epoch := format_time(0, "%Y")
    print("Epoch year: " + epoch)
    return 0
}
