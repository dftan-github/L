// Pollution guard: comments, string data and LCMD-ish text must never reach the
// AST / symbol tables, and type-driven features (Variant unboxing) must key off
// the type system, not identifier spelling.

/* 恶意块注释：Variant v; return v; v.as_string().unwrap() */
// 行注释：return this.get("app_version") ?? "0.1.0"
// LCMD_not_a_real_hook("Variant v; return v")
class Box {
    delegate CPropertys

    fun init() {
        this.set("k", "text")
    }

    // Locals spelled like type names must not hijack type-driven lowering.
    pub fun shadowed() -> String {
        Variant := String("shadow")
        return Variant
    }

    // A real Variant still unboxes through the return context.
    pub fun real() -> String {
        return this.get("k") ?? "unset"
    }
}

fun main() -> i32 {
    /* 块注释干扰：return 0; */
    i32 plain = 41
    s := "Variant v = 0; return v.as_string()"
    print(s)
    print(plain + 1)

    b := Box()
    print(b.shadowed())
    print(b.real())
    return 0
}
