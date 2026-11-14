class Engine {
    fun start() {
        print("engine")
    }
}

// Two delegates of the SAME type cannot be disambiguated (`c.Engine.start()`
// would match both embedded fields), so this MUST be rejected at compile time.
class Car {
    delegate Engine
    delegate Engine
}

fun main() -> i32 {
    c := Car()
    c.start()
    return 0
}
