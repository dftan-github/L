// `%` remainder operator: same precedence as `*` and `/`, signed `srem` on ints.
//
// Covers integer modulo, precedence/associativity, i64 width, negative operands
// (sign follows the dividend, matching C/L), and f64 `frem`.

fun main() -> i32 {
    // Basic integer modulo.
    print(7 % 3)
    print(10 % 5)
    print(0 % 4)

    // Same precedence as * and /: 2 + 7 % 3 * 2 == 2 + ((7 % 3) * 2) == 4
    print(2 + 7 % 3 * 2)

    // Left associativity: (10 % 7) % 2 == 1
    print(10 % 7 % 2)

    // Signed semantics: remainder takes the sign of the dividend.
    print(-7 % 3)
    print(7 % -3)

    // i64 width is preserved (no truncation to i32).
    i64 big = 1000000000007
    print(big % 1000)

    // f64 remainder.
    f64 f = 7.5 % 2.0
    print(f)

    // Modulo on a loop counter drives a cycle.
    total := 0
    i32 i = 0
    while i < 10 {
        if i % 2 == 0 {
            total = total + i
        }
        i = i + 1
    }
    print(total)
    return 0
}
