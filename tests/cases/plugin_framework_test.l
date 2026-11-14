// Chained calls on interface values + `this` keyword + for-in over a field.
// Covers: this.field / this.method() / delegate promotion via this.set,
// `for p in this.plugins`, `for p in obj.plugins`, empty array, and
// chained dispatch `plugins.get(i).method()` (void, i32, and String returns).

interface IPlugin {
    fun on_start()
    fun get_name() -> String
    fun id() -> i32
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

    fun id() -> i32 {
        return 1
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

    fun id() -> i32 {
        return 2
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

    // ---- chained dispatch: no temp variable, straight off the element ----
    pub fun start_one(i32 i) {
        this.plugins.get(i).on_start()
    }

    pub fun name_at(i32 i) -> String {
        return this.plugins.get(i).get_name()
    }

    pub fun id_at(i32 i) -> i32 {
        return this.plugins.get(i).id()
    }

    pub fun version() -> String {
        v := this.get("app_version") ?? Variant.from_string(String("unset"))
        return v.as_string() ?? String("unset")
    }
}

fun main() -> i32 {
    app := App()

    // Edge 1: empty array — looping over a field with zero elements is a no-op.
    app.start_all()
    print(app.counter_value())

    app.register(LoggerPlugin())
    app.register(AuthPlugin())
    print(app.counter_value())
    print(app.version())

    app.start_all()
    print(app.name_at(0))
    print(app.name_at(1))
    print(app.id_at(1))

    // Chained void method directly on the array element.
    app.start_one(0)

    // for-in over a field through a non-`this` receiver.
    for p in app.plugins {
        print(p.get_name())
    }
    return 0
}
