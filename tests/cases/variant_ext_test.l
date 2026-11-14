fun main() -> i32 {
    v1 := Variant.from_f64(3.14)
    v2 := Variant.from_bool(true)

    if v1.is_f64() {
        opt_f := v1.as_f64()
        print(opt_f ?? 0.0)
    }
    if v2.is_bool() {
        opt_b := v2.as_bool()
        print(opt_b ?? false)
    }

    // Wrong-tag check
    if !v1.is_bool() {
        print("v1 is not bool")
    }
    return 0
}
