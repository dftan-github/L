fun main() -> i32 {
    s := String("  Hello World  ")
    t := s.trim()
    hello := String("Hello")
    world := String("World")
    if t.contains(hello) == false {
        print("fail contains")
        return 1
    }
    if t.starts_with(hello) == false {
        print("fail starts")
        return 1
    }
    if t.ends_with(world) == false {
        print("fail ends")
        return 1
    }
    r := t.replace(world, String("L"))
    print(r)
    csv := String("a,b,c")
    parts := csv.split(44)
    dash := String("-")
    // Manual join (stdlib str_join deferred — Array.get type infer edge).
    a0 := parts.get(0)
    a1 := parts.get(1)
    a2 := parts.get(2)
    t0 := a0.concat(dash)
    t1 := t0.concat(a1)
    t2 := t1.concat(dash)
    j := t2.concat(a2)
    print(j)
    print(from_i64(42))
    print(str_from_bool(true))
    return 0
}
