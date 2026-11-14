// String equality through `Array<String>` element values.
//
// Regression: `names.get(i) == probe` used to lower to a `getelementptr` on a
// `%string_String` VALUE (Array<String> elements travel by value), which made
// the module fail verification (`function verification failed`). Codegen now
// spills the value into a stack slot before taking the field address.

fun has(Array<String> names, String probe) -> bool {
    i32 i = 0
    while i < names.len() {
        if names.get(i) == probe {
            return true;
        }
        i = i + 1
    }
    return false
}

fun count_matches(Array<String> names, String probe) -> i32 {
    i32 hits = 0
    i32 i = 0
    while i < names.len() {
        if names.get(i) == probe {
            hits = hits + 1
        }
        i = i + 1
    }
    return hits
}

fun main() -> i32 {
    names := Array<String>()
    names.push("db")
    names.push("single")
    names.push("http")
    names.push("db")

    // Element vs. String object.
    print(names.get(0) == String("db"))
    print(names.get(0) == String("singl"))

    // Element vs. element, across differing lengths.
    print(names.get(1) == names.get(2))
    print(names.get(0) == names.get(3))

    // Negation must invert exactly.
    print(names.get(0) != String("db"))
    print(names.get(0) != String("nope"))

    // Literal on the other side.
    print(names.get(2) == "http")

    // Loops over element comparisons.
    print(has(names, "http"))
    print(has(names, "nope"))
    print(count_matches(names, "db"))

    // Empty array: no element is ever equal.
    empty := Array<String>()
    print(has(empty, "anything"))
    print(empty.len())
    return 0
}
