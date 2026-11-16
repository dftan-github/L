// Regression: a `:=` local that becomes a boxed pointer must not overflow its
// 8-byte slot when a method hands back the class by value.
//
// `line := String(...) + from(n)` boxes `line` to `ptr`. A later
// `line = line.concat(...)` returns the whole String struct; storing that into
// the pointer slot used to corrupt neighbouring stack locals (0xC0000005).
fun main() -> i32 {
    total := 3
    line := String("total ") + from(total)
    if line.length() == 0 {
        return 1
    }

    line = line.concat(String(", passed ")).concat(from(7))
    want := String("total 3, passed 7")
    if line != want {
        print(line)
        print(String("fail: first reassign"))
        return 1
    }

    // A second reassignment must still be safe (no accumulated overflow).
    line = line.concat(String("!"))
    if "total 3, passed 7!" != line {
        print(line)
        print(String("fail: second reassign"))
        return 1
    }

    // Neighbouring locals must survive the assignment untouched.
    sentinel := 12345
    other := String("keep")
    line = line.concat(String(" end"))
    if sentinel != 12345 {
        print(String("fail: sentinel corrupted"))
        return 1
    }
    if "keep" != other {
        print(String("fail: neighbour corrupted"))
        return 1
    }

    print(String("str_reassign_ok"))
    return 0
}
