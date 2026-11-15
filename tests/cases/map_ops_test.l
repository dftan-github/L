// Map<V> key ownership + contains / remove.
//
// The important part is the dangling-pointer regression: keys built from
// temporary strings (a + b) must stay valid after the temporary is auto-freed.
fun build_key(i32 n) -> String {
    return String("k") + n
}

fun main() -> i32 {
    m := Map<i32>()

    // Temporary keys: if the map borrowed String buffers these reads are UB.
    i! := 0
    while i < 6 {
        m.put(build_key(i), i * 100)
        i! = i + 1
    }
    print(m.len())

    i! = 0
    while i < 6 {
        v := m.get(build_key(i))
        print(v ?? -1)
        i! = i + 1
    }

    // contains
    print(m.contains("k3"))
    print(m.contains("k6"))

    // Overwrite keeps size stable and replaces the value.
    m.put(build_key(3), 333)
    print(m.len())
    v3 := m.get("k3")
    print(v3 ?? -1)

    // Deleting must not break probe chains for the surviving neighbours.
    m.remove("k2")
    m.remove("k0")
    print(m.len())
    print(m.contains("k2"))

    i! = 0
    while i < 6 {
        r := m.get(build_key(i))
        print(r ?? -1)
        i! = i + 1
    }

    // Removing a missing key is a no-op.
    m.remove("nope")
    print(m.len())

    // Growth after removal still rehashes correctly.
    i! = 6
    while i < 30 {
        m.put(build_key(i), i)
        i! = i + 1
    }
    print(m.len())
    r := m.get("k29")
    print(r ?? -1)
    return 0
}
