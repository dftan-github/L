import "stdlib/array.l"
import "stdlib/optional.l"

// Two levels of nesting is allowed: Array<Optional<i32>>.
fun main() -> i32 {
    a := Array<Optional<i32>>()
    a.push(Optional<i32>(true, 7))
    a.push(Optional<i32>(false, 0))
    print(a.len())
    return 0
}
