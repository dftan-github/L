// A minimal plugin framework written purely in L.
// Demonstrates interfaces, explicit implementation (no duck typing),
// Array<IPlugin> polymorphism, `this`-based instance access,
// for-in over a member field, and chained calls on interface values
// (`plugins.get(i).on_start()`).

interface IPlugin {
    fun on_start()
    fun get_name() -> String
}

class LoggerPlugin : IPlugin {
    fun init() {
    }

    fun on_start() {
        print(String("[start] logger ready"))
    }

    fun get_name() -> String {
        return String("logger")
    }
}

class AuthPlugin : IPlugin {
    fun init() {
    }

    fun on_start() {
        print(String("[start] auth ready"))
    }

    fun get_name() -> String {
        return String("auth")
    }
}

class App {
    Array<IPlugin> plugins
    delegate CPropertys

    fun init() {
        plugins = Array<IPlugin>()
        this.set("app_version", Variant.from_string(String("0.1.0")))
    }

    pub fun register(IPlugin p) {
        this.plugins.push(p)
    }

    pub fun start_all() {
        for p in this.plugins {
            p.on_start()
        }
    }

    // Chained dispatch straight off the array element — no temp variable.
    pub fun start_one(i32 i) {
        this.plugins.get(i).on_start()
    }

    pub fun name_at(i32 i) -> String {
        return this.plugins.get(i).get_name()
    }

    pub fun plugin_count() -> i32 {
        return this.plugins.len()
    }

    pub fun version() -> String {
        v := this.get("app_version") ?? Variant.from_string(String("unset"))
        return v.as_string() ?? String("unset")
    }
}

fun main() -> i32 {
    app := App()

    print(String("app version: ") + app.version())

    app.register(LoggerPlugin())
    app.register(AuthPlugin())
    print(app.plugin_count())

    app.start_all()

    // Chained: element -> method call, both void and String-returning.
    app.start_one(0)
    print(app.name_at(1))

    for p in app.plugins {
        print(p.get_name())
    }
    return 0
}
