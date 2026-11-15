// `secret` has no `pub`, so `mod_math::secret()` must be rejected:
// [ERROR] ... 'secret' is not a pub function of module 'mod_math'
import "mod_math.l"

fun main() -> i32 {
    print(mod_math::secret())
    return 0
}
