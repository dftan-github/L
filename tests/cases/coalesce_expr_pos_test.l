// `??` on Optional<Variant> in EXPRESSION position (not just `return`).
//
// Regression: the contextual Variant unbox used to switch the builder into a
// side block, so `s := v ?? "x"` / `print(v ?? "x")` emitted the following
// instructions into a block that had already been branched away from — the
// module then failed verification with a stray `sext ptr %coalesce to i32`.

// Return position (the original, already-working path).
fun pick(CPropertys p, String key) -> String {
    return p.get(key) ?? String("RET")
}

// Decl position with an explicit String target.
fun pick_decl(CPropertys p, String key) -> String {
    String s = p.get(key) ?? String("DECL")
    return s
}

// Decl position with a primitive target.
fun pick_int(CPropertys p, String key) -> i32 {
    i32 n = p.get(key) ?? 0 - 1
    return n
}

fun main() -> i32 {
    p := CPropertys()
    p.set("name", "value")
    p.set("count", 42)

    // Return position.
    print(pick(p, "name"))
    print(pick(p, "absent"))

    // Decl position, String payload present and absent.
    print(pick_decl(p, "name"))
    print(pick_decl(p, "absent"))

    // Decl position, primitive payload present and absent.
    print(pick_int(p, "count"))
    print(pick_int(p, "absent"))

    // Expression position: `??` inside a call argument.
    print(p.get("name") ?? String("ARG"))
    print(p.get("absent") ?? String("ARG"))

    // `??` feeding a String concatenation (explicit target keeps the payload a String).
    String text = p.get("name") ?? String("none")
    print(String("concat: ") + text)

    // print(Optional<Variant>) directly: payload when present, blank when absent.
    print(p.get("name"))
    print(p.get("absent"))
    return 0
}
