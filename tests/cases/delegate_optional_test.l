// 边界 4a: delegate 提升的方法返回 Optional<T>, 必须能链式 .unwrap() 与配合 ??.
class Registry {
    Map<String> names

    fun init() {
        names = Map<String>()
    }

    pub fun lookup(String key) -> Optional<String> {
        return names.get(key)
    }

    pub fun remember(String key, String val) {
        names.put(key, val)
    }
}

class Directory {
    delegate Registry
    i32 version
}

fun main() -> i32 {
    d := Directory()

    // 空状态: delegate 返回空 Optional -> ?? fallback, 不崩溃.
    dflt := d.lookup("nobody") ?? "anonymous"
    print(dflt)                         // anonymous

    // 链式: delegate Optional 直接 .unwrap() 的值语义.
    d.remember("boss", "Alice")
    found := d.lookup("boss")
    print(found.unwrap())               // Alice

    // 链式 + 幂等: 再取一次仍然正确 (无状态污染).
    print(d.lookup("boss") ?? "none")   // Alice

    // 存在时 ?? 不触发 fallback.
    hit := d.lookup("boss") ?? "fallback"
    if hit.length() > 0 {
        print(hit)                      // Alice
    }

    return 0
}
