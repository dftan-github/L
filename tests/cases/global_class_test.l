class App {
    pub i32 version
    fun init() {
        version = 1
    }
}

// Global class instance — zero-init storage, App() / init runs in __l_global_init.
App my_app = App()

fun main() -> i32 {
    print(my_app.version)
    my_app.version = 2
    print(my_app.version)

    my_app {
        .version = 3
    }
    print(my_app.version)
    return 0
}
