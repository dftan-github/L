// break / continue in while, for-in over Array and over a range.
// Also covers `??` with a negative literal at argument position.

fun main() -> i32 {
    // 1) while: skip 3, stop before 7 -> 1+2+4+5+6 = 18, i ends at 7
    i! := 0
    sum! := 0
    while i < 10 {
        i = i + 1
        if i == 3 {
            continue
        }
        if i > 6 {
            break
        }
        sum = sum + i
    }
    print(sum)
    print(i)

    // 2) for-in over Array: skip 2, break at 4 -> 1+3 = 4
    arr := Array<i32>()
    arr.push(1)
    arr.push(2)
    arr.push(3)
    arr.push(4)
    total! := 0
    for x in arr {
        if x == 2 {
            continue
        }
        if x == 4 {
            break
        }
        total = total + x
    }
    print(total)

    // 3) for-in over range: skip 2, break at 5 -> prints 0 1 3 4
    for k in 0..10 {
        if k == 2 {
            continue
        }
        if k == 5 {
            break
        }
        print(k)
    }

    // 4) break only leaves the innermost loop
    outer! := 0
    while outer < 2 {
        inner! := 0
        while inner < 5 {
            if inner == 2 {
                break
            }
            inner = inner + 1
        }
        print(outer * 10 + inner)
        outer = outer + 1
    }

    // 5) defer still runs exactly once when the body exits via break
    n! := 0
    while n < 10 {
        defer print(String("cleanup"))
        n = n + 1
        if n == 2 {
            break
        }
    }
    print(n)

    // 6) `??` with a negative literal fallback
    empty := Optional<i32>(false, 0)
    print(empty ?? -1)

    full := Optional<i32>(true, 42)
    print(full ?? -1)

    // 7) plain negative literals (int and float) still parse
    print(-7)
    print(-2.5)
    return 0
}
