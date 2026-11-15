import "stdlib/string.l"
import "stdlib/result.l"

// --- 1. `Result<bool, E>` as the first type argument ---------------------
// `bool` is a type keyword, not an Identifier; it must still parse as the first
// generic argument in both type and constructor positions.
fun check_positive(i32 n) -> Result<bool, String> {
    if n > 0 {
        return Result<bool, String>(true, true, String(""))
    }
    return Result<bool, String>(false, false, String("not positive"))
}

// Recursive helper: the old workaround returned `Result<i32, String>` because
// `bool` could not be written in the first slot.
fun all_positive(Array<i32> xs, i32 i) -> Result<bool, String> {
    if i >= xs.len() {
        return Result<bool, String>(true, true, String(""))
    }
    inner := check_positive(xs.get(i))
    if !inner.is_ok() {
        return Result<bool, String>(false, false, inner.unwrap_err())
    }
    return all_positive(xs, i + 1)
}

// --- 2. `else if` chains ------------------------------------------------
fun grade(i32 score) -> i32 {
    if score >= 90 {
        return 4
    } else if score >= 80 {
        return 3
    } else if score >= 70 {
        return 2
    } else if score >= 60 {
        return 1
    } else {
        return 0
    }
}

// `else if` nested inside a value-returning expression position.
fun describe(i32 n) -> String {
    label := "neg"
    if n == 0 {
        label = "zero"
    } else if n < 10 {
        label = "small"
    } else if n < 100 {
        label = "medium"
    } else {
        label = "large"
    }
    return label
}

// --- 3. Bare `return;` (ASI) --------------------------------------------
fun early_exit(i32 n) {
    if n > 5 {
        print("early")
        return
    }
    print("normal")
}

fun early_exit_block(i32 n) {
    if n > 5 {
        return
    }
}

fun void_with_trailing_return(i32 n) {
    print(n)
    return
}

fun main() -> i32 {
    // 1. Result<bool, E>
    r1 := check_positive(7)
    if r1.is_ok() && r1.unwrap() {
        print("ok result_bool_some")
    }
    r2 := check_positive(-3)
    if !r2.is_ok() {
        print(r2.unwrap_err())
    }

    xs := Array<i32>()
    xs.push(1)
    xs.push(2)
    xs.push(3)
    r3 := all_positive(xs, 0)
    if r3.is_ok() && r3.unwrap() {
        print("ok result_bool_recursive")
    }
    bad := Array<i32>()
    bad.push(1)
    bad.push(-1)
    r4 := all_positive(bad, 0)
    if !r4.is_ok() {
        print(r4.unwrap_err())
    }

    // 2. else if
    print(grade(95))
    print(grade(85))
    print(grade(75))
    print(grade(65))
    print(grade(10))
    print(describe(-4))
    print(describe(0))
    print(describe(7))
    print(describe(70))
    print(describe(700))

    // 3. bare return
    early_exit(9)
    early_exit(2)
    early_exit_block(9)
    print("after early block")
    void_with_trailing_return(3)
    return 0
}
