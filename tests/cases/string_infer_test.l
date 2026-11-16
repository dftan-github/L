// [A004] Bare string literals must infer to String without any `String(...)`
// call in the program (no constructor side effect to trigger type loading).
// Linked with stdlib/string.l explicitly by the runner.

fun main() -> i32 {
    a := "hello"
    print(a)

    if "hello" == a {
        print("eq-ok")
    }

    n := a.length()
    print(n)

    b := a + "!"
    print(b)

    return 0
}
