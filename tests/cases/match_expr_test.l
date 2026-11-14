// Value-producing match with Optional destructuring.
//   match opt { some(v) => expr, none => expr }
// Arms merge through a PHI node; `some` binds the payload to `v`.

class Box {
    pub i32 v
    fun init(i32 x) {
        v = x
    }
    fun doubled() -> i32 {
        return v * 2
    }
}

fun some_int() -> Optional<i32> {
    return Optional<i32>(true, 42)
}

fun no_int() -> Optional<i32> {
    return Optional<i32>(false, 0)
}

fun some_str() -> Optional<String> {
    return Optional<String>(true, String("hello"))
}

fun no_str() -> Optional<String> {
    return Optional<String>(false, String(""))
}

fun some_box() -> Optional<Box> {
    return Optional<Box>(true, Box(21))
}

fun classify(i32 n) -> i32 {
    // Statement-position match still works unchanged.
    match n {
        1 => {
            print(String("one"))
        }
        _ => {
            print(String("many"))
        }
    }
}

fun main() -> i32 {
    a := some_int()
    b := no_int()

    // Primitive payload extracted and folded into the arm value.
    print(match a { some(v) => v + 1, none => 0 })
    print(match b { some(v) => v + 1, none => 0 })

    // Arm value reused as a normal expression result.
    x := match a { some(v) => v * 2, none => -1 }
    print(x)

    // String arms merge as String values.
    print(match some_str() { some(s) => s, none => String("missing") })
    print(match no_str() { some(s) => s, none => String("missing") })

    // Class payload: the binding is a real local with method dispatch.
    print(match some_box() { some(bx) => bx.doubled(), none => 0 })

    // Literal arms also yield values; `_` guards the fallthrough.
    n := 2
    print(match n { 1 => 10, 2 => 20, _ => 30 })
    // No arm matched: placeholder keeps the PHI well-formed.
    print(match n { 1 => 10, _ => 20 })

    classify(1)
    classify(9)
    return 0
}
