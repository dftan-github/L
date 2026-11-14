// Read-side auto-boxing: `Optional<Variant> ?? <primitive|String>`.
// When the left operand is `Optional<Variant>`, a primitive/String fallback is
// implicitly boxed via Variant.from_*, mirroring the write-side `set(...)` sugar.

class Store {
    delegate CPropertys

    fun init() {
        this.set("name", "demo")
        this.set("count", 42)
        this.set("ratio", 1.5)
        this.set("enabled", true)
    }

    pub fun name() -> String {
        return (this.get("name") ?? "fallback").as_string() ?? String("?")
    }

    pub fun count() -> i32 {
        return (this.get("count") ?? 0).as_i32() ?? 0
    }

    pub fun ratio() -> f64 {
        return (this.get("ratio") ?? 0.0).as_f64() ?? 0.0
    }

    pub fun enabled() -> bool {
        return (this.get("enabled") ?? false).as_bool() ?? false
    }

    // Missing key → the fallback must be boxed instead of short-circuiting.
    pub fun missing_name() -> String {
        return (this.get("nope") ?? "unset").as_string() ?? String("?")
    }

    pub fun missing_count() -> i32 {
        return (this.get("nope") ?? 99).as_i32() ?? 0
    }

    pub fun missing_ratio() -> f64 {
        return (this.get("nope") ?? 2.5).as_f64() ?? 0.0
    }

    pub fun missing_enabled() -> bool {
        return (this.get("nope") ?? true).as_bool() ?? false
    }
}

fun main() -> i32 {
    s := Store()
    print(s.name())
    print(s.count())
    print(s.ratio())
    print(s.enabled())
    print(s.missing_name())
    print(s.missing_count())
    print(s.missing_ratio())
    print(s.missing_enabled())

    // Bare Optional<Variant> local, no property indirection.
    opt := Optional<Variant>(false, Variant.from_i32(0))
    print((opt ?? 7).as_i32() ?? 0)
    return 0
}
