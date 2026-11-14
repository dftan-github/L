// Implicit auto-boxing into Variant.
// `set("k", <primitive or string>)` lowers to Variant.from_<kind>(...),
// so business code never spells the wrapper out by hand.

class Config {
    delegate CPropertys

    fun init() {
        // Literals of every supported primitive kind.
        this.set("name", "demo")
        this.set("count", 42)
        this.set("ratio", 1.5)
        this.set("enabled", true)
    }

    pub fun name() -> String {
        return (this.get("name") ?? Variant.from_string(String("?"))).as_string() ?? String("?")
    }

    pub fun count() -> i32 {
        return (this.get("count") ?? Variant.from_i32(0)).as_i32() ?? 0
    }

    pub fun ratio() -> f64 {
        return (this.get("ratio") ?? Variant.from_f64(0.0)).as_f64() ?? 0.0
    }

    pub fun enabled() -> bool {
        return (this.get("enabled") ?? Variant.from_bool(false)).as_bool() ?? false
    }
}

fun main() -> i32 {
    c := Config()
    print(c.name())
    print(c.count())
    print(c.ratio())
    print(c.enabled())

    // Non-literal arguments (locals) get boxed the same way.
    k := 7
    s := String("via-local")
    c.set("k2", k)
    c.set("s2", s)
    b := true
    c.set("b2", b)
    print((c.get("k2") ?? Variant.from_i32(0)).as_i32() ?? 0)
    print((c.get("s2") ?? Variant.from_string(String("?"))).as_string() ?? String("?"))
    print((c.get("b2") ?? Variant.from_bool(false)).as_bool() ?? false)
    return 0
}
