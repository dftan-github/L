fun main() -> i32 {
    v1 := Variant.from_i32(42)
    v2 := Variant.from_string("Hello")

    // Type check
    if v1.is_i32() {
        print("v1 is i32")
    }

    // Extract matching tag
    x := v1.as_i32()
    print(x ?? 0)

    // Extract mismatch → empty Optional
    y := v2.as_i32()
    print(y ?? 99)

    return 0
}
