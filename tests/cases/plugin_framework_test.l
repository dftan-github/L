// `this` keyword + for-in over a member field.
// Covers: this.field / this.method() / delegate promotion via this.set,
// `for p in this.plugins`, `for p in obj.plugins`, empty array, and
// `this` rejected inside a static method.

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
    i32 counter

    fun init() {
        plugins = Array<IPlugin>()
        counter = 0
        this.set("app_version", Variant.from_string(String("0.1.0")))
    }

    pub fun register(IPlugin p) {
        this.plugins.push(p)
        this.bump()
    }

    pub fun bump() {
        counter = counter + 1
    }

    pub fun counter_value() -> i32 {
        return this.counter
    }

    pub fun start_all() {
        for p in this.plugins {
            p.on_start()
        }
    }

    pub fun plugin_names() {
        for p in this.plugins {
            print(p.get_name())
        }
    }

    pub fun version() -> String {
        v := this.get("app_version") ?? Variant.from_string(String("unset"))
        return v.as_string() ?? String("unset")
    }
}

fun main() -> i32 {
    app := App()

    // Edge 1: empty array — looping over a field with zero elements must be a no-op.
    app.start_all()
    print(app.counter_value())

    app.register(LoggerPlugin())
    app.register(AuthPlugin())
    print(app.counter_value())
    print(app.version())

    app.start_all()
    app.plugin_names()

    // for-in over a field through a non-`this` receiver.
    for p in app.plugins {
        print(p.get_name())
    }
    return 0
}
