class User {
    delegate CPropertys
    i32 id
}

fun main() -> i32 {
    u := User()

    // ---- 边界 1: 空 bag, 任何 key 都拿不到 -> 走 ?? 默认值 ----
    early := u.get("nothing") ?? Variant.from_string("empty")
    if early.is_string() {
        print(early.as_string().unwrap()) // 输出 empty
    }

    // ---- 写入 ----
    u.set("role", Variant.from_string("admin"))
    u.set("age", Variant.from_i32(20))

    // 读取（配合 ?? 语法糖）
    role_v := u.get("role") ?? Variant.from_string("guest")
    if role_v.is_string() {
        print(role_v.as_string().unwrap()) // 输出 admin
    }

    // 测试取不到的情况
    name_v := u.get("name") ?? Variant.from_string("unknown")
    if name_v.is_string() {
        print(name_v.as_string().unwrap()) // 输出 unknown
    }

    // ---- 边界 2: 同 key 重复写入, 后写覆盖前写 ----
    u.set("role", Variant.from_string("root"))
    overwritten := u.get("role") ?? Variant.from_string("gone")
    if overwritten.is_string() {
        print(overwritten.as_string().unwrap()) // 输出 root
    }

    // ---- 边界 3: 类型不匹配, 存 i32 却按 String 取 ----
    neg := 0 - 1
    age_v := u.get("age") ?? Variant.from_i32(neg)
    if age_v.is_string() {
        print("unexpected-string")
    }
    if age_v.is_i32() {
        print(age_v.as_i32().unwrap()) // 输出 20
    }

    // ---- 边界 4: 存 i32 后重新按 String 存, tag 必须翻转 ----
    u.set("age", Variant.from_string("twenty"))
    flipped := u.get("age") ?? Variant.from_string("?")
    if flipped.is_string() {
        print(flipped.as_string().unwrap()) // 输出 twenty
    }

    // ---- 边界 5: 负数与 0 的 payload 不能被当成空 ----
    u.set("zero", Variant.from_i32(0))
    zero_v := u.get("zero") ?? Variant.from_i32(999)
    print(zero_v.as_i32().unwrap()) // 输出 0

    // ---- 边界 6: 反向类型突变 — 存 String 却按 i32 读 ----
    // 必须安全落空到 fallback, 绝不能把 String 的指针当 i32 解引用.
    str_key := u.get("role") ?? Variant.from_string("gone")
    if str_key.is_i32() {
        print("BUG: string read as i32")
    } else {
        print("string-not-i32")      // 输出 string-not-i32
    }
    as_num := str_key.as_i32()
    if as_num.is_some() {
        print("BUG: as_i32 on string returned some")
    } else {
        print("string-as-i32-empty") // 输出 string-as-i32-empty
    }

    // ---- 边界 7: 空 Optional 直接 unwrap 必须是受控行为, 不越界读 ----
    // 用 isEmpty 判定代替裸 unwrap, 确保判空只看 has_value.
    missing := u.get("definitely-absent")
    if missing.is_some() {
        print("BUG: absent key reported some")
    } else {
        print("absent-is-empty")     // 输出 absent-is-empty
    }

    // ---- 边界 8: 连续覆盖 3 次, 只保留最后一次 ----
    u.set("k", Variant.from_i32(1))
    u.set("k", Variant.from_i32(2))
    u.set("k", Variant.from_i32(3))
    last := u.get("k") ?? Variant.from_i32(0)
    print(last.as_i32().unwrap())    // 输出 3

    // ---- 边界 9: f64 与 bool 也遵守同样的判空规则 ----
    u.set("pi", Variant.from_f64(0.0))
    pi_v := u.get("pi") ?? Variant.from_f64(1.5)
    print(pi_v.as_f64().unwrap())    // 输出 0

    u.set("flag", Variant.from_bool(false))
    flag_v := u.get("flag") ?? Variant.from_bool(true)
    if flag_v.is_bool() {
        print(flag_v.as_bool().unwrap()) // 输出 0
    }

    return 0
}
