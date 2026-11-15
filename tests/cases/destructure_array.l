import "stdlib/array.l"
import "stdlib/optional.l"

// Array destructuring patterns (`A474`):
//   `[a, b] => ...` binds the leading elements; longer arrays still match.

fun head_tail(Array<i32> xs) {
    match xs {
        [a, b] => {
            print(a)
            print(b)
        }
        _ => { print("short") }
    }
}

fun first_only(Array<String> names) {
    match names {
        [first] => { print(first) }
        _ => { print("none") }
    }
}

// Skip an element with `_` while keeping the prefix requirement.
fun second(Array<i32> xs) -> i32 {
    v := match xs {
        [_, b] => b
        _ => -1
    }
    return v
}

fun main() -> i32 {
    xs := Array<i32>()
    xs.push(10)
    xs.push(20)
    xs.push(30)
    head_tail(xs)

    short := Array<i32>()
    short.push(7)
    head_tail(short)

    names := Array<String>()
    names.push("Ada")
    first_only(names)

    print(second(xs))
    print(second(short))
    return 0
}
