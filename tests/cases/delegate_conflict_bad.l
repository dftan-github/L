class Engine {
    fun start() {
        print("engine")
    }
}

class Motor {
    fun start() {
        print("motor")
    }
}

class Car {
    delegate Engine
    delegate Motor
}

fun main() -> i32 {
    c := Car()
    c.start()
    return 0
}
