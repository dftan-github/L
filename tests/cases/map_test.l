fun main() -> i32 {
    m := Map<i32>()
    m.put("one", 1)
    m.put("two", 2)
    m.put("three", 3)

    v1 := m.get("two")
    print(v1 ?? 0)

    v2 := m.get("four")
    print(v2 ?? 99)
    return 0
}
