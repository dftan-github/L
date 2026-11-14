// Performance smoke: CPropertys (recursive tree, lazy child Map, Variant values).

fun bench_tree_build() -> i32 {
    // 20_000 nodes: 1 root + 20_000 children, each holding an i32 payload.
    root := CPropertys()
    i! := 0
    while i < 20000 {
        n := CPropertys()
        n.set_val(Variant.from_i32(i))
        root.add_child(String.from(i), n)
        i! += 1
    }
    return root.child_count()
}

fun bench_tree_lookup() -> i32 {
    // 1_000_000 hashed lookups over a 20_000-entry child map.
    root := CPropertys()
    i! := 0
    while i < 20000 {
        n := CPropertys()
        n.set_val(Variant.from_i32(i))
        root.add_child(String.from(i), n)
        i! += 1
    }
    i64 sum! = 0
    i! = 0
    while i < 1000000 {
        key := String.from(i - (i / 20000) * 20000)
        if root.get_child(key).is_some() {
            sum! += 1
        }
        i! += 1
    }
    return sum as i32
}

fun bench_tree_deep() -> i32 {
    // 1_000-deep chain, then 1_000 full root-to-leaf walks (1_000_000 hops).
    root := CPropertys()
    cur := root
    i! := 0
    while i < 1000 {
        n := CPropertys()
        n.set_val(Variant.from_i32(i))
        cur.add_child("c", n)
        cur = n
        i! += 1
    }

    path := "c"
    i! = 1
    while i < 1000 {
        path = path + ".c"
        i! += 1
    }

    i64 hops! = 0
    i! = 0
    while i < 1000 {
        if root.get_path(path).is_some() {
            hops! += 1
        }
        i! += 1
    }
    return hops as i32
}

fun main() -> i32 {
    args := get_args()
    mode := "all"
    if args.len() > 1 {
        mode = args.get(1)
    }

    if mode == "build" || mode == "all" {
        print("bench_tree_build begin")
        print(bench_tree_build())
        print("bench_tree_build end")
    }

    if mode == "lookup" || mode == "all" {
        print("bench_tree_lookup begin")
        print(bench_tree_lookup())
        print("bench_tree_lookup end")
    }

    if mode == "deep" || mode == "all" {
        print("bench_tree_deep begin")
        print(bench_tree_deep())
        print("bench_tree_deep end")
    }
    return 0
}
