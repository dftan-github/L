// A minimal plugin framework written purely in L.
// Demonstrates interfaces, explicit implementation (no duck typing),
// Array<IPlugin> polymorphism, `this`-based instance access, and
// for-in over a member field (`this.plugins` / `app.plugins`).

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
        // `this.set` resolves through the delegated CPropertys.
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

    pub fun plugin_count() -> i32 {
        return this.plugins.len()
    }

    pub fun plugin_name_at(i32 i) -> String {
        IPlugin p = this.plugins.get(i)
        return p.get_name()
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

    // Iterating a field through an arbitrary receiver (not `this`).
    for p in app.plugins {
        print(p.get_name())
    }
    return 0
}
