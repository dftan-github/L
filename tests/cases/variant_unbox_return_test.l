// Contextual Variant unboxing on `return`.
// Returning a Variant from a function whose signature names a concrete type
// lowers to `v.as_<t>()` + a tag check that panics on mismatch.

class Store {
    delegate CPropertys

    fun init() {
        this.set("name", "demo")
        this.set("count", 42)
        this.set("ratio", 1.5)
        this.set("enabled", true)
    }

    // The whole point of this feature: no as_*()/unwrap() noise.
    pub fun name() -> String {
        return this.get("name") ?? "unset"
    }

    pub fun count() -> i32 {
        return this.get("count") ?? 0
    }

    pub fun ratio() -> f64 {
        return this.get("ratio") ?? 0.0
    }

    pub fun enabled() -> bool {
        return this.get("enabled") ?? false
    }

    // Missing key → fallback is boxed, then unboxed by the return context.
    pub fun missing_name() -> String {
        return this.get("nope") ?? "fallback"
    }

    // Straight Variant return without `??`.
    pub fun direct_count() -> i32 {
        v := this.get("count") ?? 0
        return v
    }
}

fun main() -> i32 {
    s := Store()
    print(s.name())
    print(s.count())
    print(s.ratio())
    print(s.enabled())
    print(s.missing_name())
    print(s.direct_count())
    return 0
}
