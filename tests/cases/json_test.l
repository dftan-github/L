fun main() -> i32 {
    // Build {"name":"L","n":42,"ok":true,"x":null} without '{' in L string literals.
    lb := String(l_str_from_byte(123))
    rb := String(l_str_from_byte(125))
    q := String(l_str_from_byte(34))
    raw := lb + q + String("name") + q + String(":") + q + String("L") + q + String(",") + q + String("n") + q + String(":42,") + q + String("ok") + q + String(":true,") + q + String("x") + q + String(":null") + rb
    parsed := json_parse_object(raw)
    if parsed.is_ok() == false {
        print("fail parse")
        return 1
    }
    m := parsed.unwrap()
    name_key := String("name")
    name := m.get(name_key)
    if name.is_some() == false {
        print("fail name")
        return 1
    }
    print(name.unwrap())
    n_key := String("n")
    n := m.get(n_key)
    print(n.unwrap())
    round := json_stringify_object(m)
    print(round)
    if m.len() < 1 {
        print("fail len")
        return 1
    }
    print(String("json_ok"))
    return 0
}
