class Engine {
    fun start() {
        print("Vroom!")
    }
}

class Car {
    delegate Engine
}

fun main() -> i32 {
    c := Car()
    c.start()
    return 0
}
