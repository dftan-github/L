class Engine {
    fun start() {
        print("engine")
    }
}

// The duplicate only becomes visible after partial merging: each part declares
// one `delegate Engine`. Merging must still reject it.
partial class Car {
    delegate Engine
}

partial class Car {
    delegate Engine
}

fun main() -> i32 {
    c := Car()
    c.start()
    return 0
}
