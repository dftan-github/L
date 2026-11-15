// `mod_math::add` is pub → callable. `mod_math::via_secret` is pub and reaches
// the private helper from inside its own module, so that path stays legal.
import "mod_math.l"

fun main() -> i32 {
    print(mod_math::add(2, 3))
    print(mod_math::via_secret())
    return 0
}
