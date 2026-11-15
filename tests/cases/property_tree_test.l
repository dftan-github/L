fun main() -> i32 {
    root := CPropertys()
    node1 := CPropertys()
    node2 := CPropertys()

    root.add_property("n1", node1)
    node1.add_property("n2", node2)
    node2.set_val(42)

    // 安全调用链 + 默认值：整条链任何一段缺失都不崩，直接落到 ??
    v := root?.get_property("n1")?.get_property("n2")?.get_val() ?? Variant.from_i32(-1)
    if v.is_i32() {
        print(v.as_i32() ?? -1)          // 输出 42
    }
    print(root.property_count())            // 输出 1

    // ---- 边界 1: 空树 / 未设值 ----
    empty := CPropertys()
    print(empty.property_count())           // 输出 0
    if empty.has_value() {
        print("BUG: fresh node has value")
    } else {
        print("empty-has-no-value")
    }

    // ---- 边界 2: 不存在的 key（?. 链短路） ----
    missing := root?.get_property("absent")?.get_val() ?? Variant.from_i32(-2)
    print(missing.as_i32() ?? -2)        // 输出 -2
    if empty.get_val().is_some() {
        print("BUG: absent value reported some")
    } else {
        print("absent-value-empty")
    }

    // ---- 边界 3: 同 key 重复写入, 后写覆盖前写 ----
    other := CPropertys()
    other.set_val(7)
    root.add_property("n1", other)
    print(root.property_count())            // 输出 1
    print(root?.get_property("n1")?.get_val()?.as_i32() ?? -1) // 输出 7

    // ---- 边界 4: 兄弟节点互不干扰 ----
    sibling := CPropertys()
    sibling.set_val(9)
    root.add_property("n3", sibling)
    print(root.property_count())            // 输出 2
    print(root?.get_property("n3")?.get_val()?.as_i32() ?? -1) // 输出 9
    if root?.get_property("n1")?.has_property("n3") ?? false {
        print("BUG: sibling leaked into n1")
    } else {
        print("siblings-isolated")
    }

    // ---- 边界 5: 引用语义 — 先挂子节点, 后改值必须可见 ----
    root.add_property("late", node2)
    node2.set_val(99)
    print(root?.get_property("late")?.get_val()?.as_i32() ?? -1) // 输出 99

    // ---- 边界 6: 叶子判定 ----
    node2.set_val(1)
    if node2.is_leaf() {
        print("node2-is-leaf")
    } else {
        print("BUG: node2 should be a leaf")
    }
    if root.is_leaf() {
        print("BUG: root should not be a leaf")
    } else {
        print("root-not-leaf")
    }

    // ---- 边界 7: 清空值后回到空状态 ----
    node2.clear_val()
    if node2.get_val().is_some() {
        print("BUG: cleared value still present")
    } else {
        print("cleared-value-empty")
    }

    // ---- 边界 8: 可变 payload 类型翻转 (i32 -> String) ----
    node2.set_val(Variant.from_string("hi"))
    print(node2?.get_val()?.as_string() ?? "BUG: tag did not flip") // 输出 hi

    // ---- 边界 9: 点分路径穿透 ----
    if (root?.get_path("n1") ?? empty).has_property("n1") {
        print("BUG: path n1 resolved to wrong node")
    } else {
        print("path-found")
    }
    bad := root?.get_path("n1.missing")?.get_val() ?? Variant.from_i32(-3)
    print(bad.as_i32() ?? -3)            // 输出 -3
    print(root?.get_path("n3")?.get_val()?.as_i32() ?? -1) // 输出 9

    return 0
}
