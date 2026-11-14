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
    // Explicit disambiguation is required when multiple delegates promote the same method.
    c.Engine.start()
    c.Motor.start()
    return 0
}
