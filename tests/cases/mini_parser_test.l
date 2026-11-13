fun print_decl(VarDeclNode d) {
    print("Type: {d.type_name}")
    print("Name: {d.name}")
    print("Value: {d.value}")
}

fun main() -> i32 {
    // 1) Typed declaration
    opt1 := parse_var_decl("i32 age = 18")
    if opt1.is_some() {
        print_decl(opt1.unwrap())
    } else {
        print("parse failed: typed")
    }

    // 2) Inferred declaration with a string literal value.
    // L keeps \" in the string body (no unescape yet), so the line is:
    //   name := \"L\"
    opt2 := parse_var_decl("name := \"L\"")
    if opt2.is_some() {
        print_decl(opt2.unwrap())
    } else {
        print("parse failed: inferred")
    }

    return 0
}
