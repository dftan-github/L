fun main() -> i32 {
    src := "let x = 42"
    tokens := tokenize(src)

    i! := 0
    while i < tokens.len() {
        t := tokens.get(i)
        if t.is_i32() {
            opt := t.as_i32()
            print(opt.unwrap_or(0))
        } else {
            opts := t.as_string()
            s := opts.unwrap_or(String(""))
            print(s)
        }
        i! = i + 1
    }
    return 0
}
