fun main() -> i32 {
    root := PropertyTree()
    node1 := PropertyTree()
    node2 := PropertyTree()

    root.add_child("n1", node1)
    node1.add_child("n2", node2)
    node2.set_val(42)

    // 链式穿透读取
    v := root.get_child("n1").unwrap().get_child("n2").unwrap().get_val().unwrap()
    if v.is_i32() {
        print(v.as_i32().unwrap())       // 输出 42
    }
    print(root.child_count())            // 输出 1

    // ---- 边界 1: 空树 / 未设值 ----
    empty := PropertyTree()
    print(empty.child_count())           // 输出 0
    if empty.has_value() {
        print("BUG: fresh node has value")
    } else {
        print("empty-has-no-value")
    }

    // ---- 边界 2: 不存在的 key ----
    if root.get_child("absent").is_some() {
        print("BUG: absent child reported some")
    } else {
        print("absent-child-empty")
    }
    if empty.get_val().is_some() {
        print("BUG: absent value reported some")
    } else {
        print("absent-value-empty")
    }

    // ---- 边界 3: 同 key 重复写入, 后写覆盖前写 ----
    other := PropertyTree()
    other.set_val(7)
    root.add_child("n1", other)
    print(root.child_count())            // 输出 1
    print(root.get_child("n1").unwrap().get_val().unwrap().as_i32().unwrap()) // 输出 7

    // ---- 边界 4: 兄弟节点互不干扰 ----
    sibling := PropertyTree()
    sibling.set_val(9)
    root.add_child("n3", sibling)
    print(root.child_count())            // 输出 2
    print(root.get_child("n3").unwrap().get_val().unwrap().as_i32().unwrap()) // 输出 9
    if root.get_child("n1").unwrap().has_child("n3") {
        print("BUG: sibling leaked into n1")
    } else {
        print("siblings-isolated")
    }

    // ---- 边界 5: 引用语义 — 先挂子节点, 后改值必须可见 ----
    root.add_child("late", node2)
    node2.set_val(99)
    print(root.get_child("late").unwrap().get_val().unwrap().as_i32().unwrap()) // 输出 99

    // ---- 边界 6: 叶子判定 ----
    node2.set_val(1)
    if node2.is_leaf() {
        print("node2-is-leaf")           // 输出 node2-is-leaf
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
    if node2.get_val().unwrap().is_string() {
        print(node2.get_val().unwrap().as_string().unwrap()) // 输出 hi
    } else {
        print("BUG: tag did not flip")
    }

    // ---- 边界 9: 点分路径穿透 ----
    path_v := root.get_path("n1")
    if path_v.is_some() {
        print("path-found")
    } else {
        print("BUG: path n1 missing")
    }
    if root.get_path("n1.missing").is_some() {
        print("BUG: bad path reported some")
    } else {
        print("path-missing-empty")
    }
    if root.get_path("n3").unwrap().get_val().unwrap().is_i32() {
        print(root.get_path("n3").unwrap().get_val().unwrap().as_i32().unwrap()) // 输出 9
    }

    return 0
}
