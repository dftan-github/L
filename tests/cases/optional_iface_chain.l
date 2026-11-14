// Optional<IFace> chain unwrapping: `opt.unwrap().method()` must dispatch
// through the fat pointer's vtable, wherever the Optional came from.
//
// Note: Array<T>.get returns a bare `T` (no bounds check), so the Optional
// sources exercised here are the ones the stdlib actually produces:
// Map.get, a function return, and a method return.

interface IPlugin {
    fun on_start()
    fun get_name() -> String
}

class Logger : IPlugin {
    fun on_start() {
        print("[start] logger")
    }

    fun get_name() -> String {
        return String("logger")
    }
}

class Auth : IPlugin {
    fun on_start() {
        print("[start] auth")
    }

    fun get_name() -> String {
        return String("auth")
    }
}

// Optional produced by a free function.
fun pick(i32 i) -> Optional<IPlugin> {
    if i == 0 {
        return Optional<IPlugin>(true, Logger())
    }
    return Optional<IPlugin>(true, Auth())
}

// Optional produced by an instance method over an Array<IPlugin>.
class Host {
    Array<IPlugin> plugins

    fun init() {
        plugins = Array<IPlugin>()
        plugins.push(Logger())
        plugins.push(Auth())
    }

    pub fun get_plugin(i32 i) -> Optional<IPlugin> {
        if i < plugins.len() {
            return Optional<IPlugin>(true, plugins.get(i))
        }
        return Optional<IPlugin>(false, Logger())
    }
}

fun main() -> i32 {
    h := Host()

    // 1. Method-returned Optional, chained straight through on_start().
    h.get_plugin(0).unwrap().on_start()
    h.get_plugin(1).unwrap().on_start()

    // 2. Same Optional, chained through a value-returning method.
    print(h.get_plugin(0).unwrap().get_name())
    print(h.get_plugin(1).unwrap().get_name())

    // 3. Free-function Optional.
    print(pick(0).unwrap().get_name())
    print(pick(1).unwrap().get_name())

    // 4. Map.get returns Optional<V> → unwrap → vtable dispatch.
    m := Map<IPlugin>()
    m.put("log", Logger())
    m.put("auth", Auth())
    m.get("log").unwrap().on_start()
    print(m.get("auth").unwrap().get_name())

    // 5. Bind first, then chain (receiver is a local holding the Optional).
    opt := h.get_plugin(1)
    opt.unwrap().on_start()
    print(opt.unwrap().get_name())

    return 0
}
