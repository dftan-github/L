// `obj?.field` — safe field access short-circuits on an empty Optional.
// Also covers implicit boxing into Array<Variant>.push.
class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }

    pub fun sum() -> i32 {
        return x + y
    }
}

fun main() -> i32 {
    p := Point(3, 4)
    opt := Optional<Point>(true, p)

    // Present receiver: field is exposed as Optional<i32>.
    print(opt?.x ?? -1)
    print(opt?.sum() ?? -1)

    // Empty receiver: the field access yields an empty Optional, no crash.
    empty := Optional<Point>()
    print(empty?.x ?? -1)
    print(empty?.y ?? -1)

    // `?.` flattened through an Optional field must not nest Optionals.
    probe := empty?.x ?? 7
    print(probe)

    // Implicit boxing: Array<Variant>.push accepts primitives / String directly.
    v := Array<Variant>()
    v.push(42)
    v.push("hello")
    v.push(1.5)
    v.push(true)
    print(v.len())
    print(v.get(0).as_i32() ?? 0)
    s := v.get(1).as_string() ?? String("")
    print(s)
    f := v.get(2).as_f64() ?? 0.0
    print(f)
    return 0
}
