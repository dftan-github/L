// Regex backtrack-stack overflow must be a hard runtime error, not a silent
// "no match". The C engine returns -1 and stdlib/regex.l turns it into a panic.
//
// Expected: prints "start", then the panic message on stderr and aborts.

fun main() -> i32 {
    print("start")

    // 300 consecutive 'a': the `a*` repetition exceeds the 256-entry position
    // stack, so the greedy scan cannot be completed reliably.
    i! := 0
    s! := String("")
    while i < 300 {
        s = s + "a"
        i = i + 1
    }

    if contains_pattern(s, "a*b") {
        print("matched")
    } else {
        print("silently truncated")
    }
    return 0
}
