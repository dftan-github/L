fun classify(i32 n) -> i32 {
    if n < 0 {
        return -1
    } else if n == 0 {
        return 0
    } else if n < 10 {
        return 1
    } else {
        return 2
    }
}

fun main() -> i32 {
    sum := 0
    i := 1
    while i <= 10 {
        sum = sum + i
        i = i + 1
    }
    if sum == 55 {
        print(999)
    } else {
        print(0)
    }
    // `else if` chains must bind to a single nested if, keeping all arms exclusive.
    if classify(-5) == -1 && classify(0) == 0 && classify(7) == 1 && classify(42) == 2 {
        print(888)
    } else {
        print(0)
    }
    return 0
}
