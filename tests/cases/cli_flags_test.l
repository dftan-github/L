fun main() -> i32 {
    flags := parse_flags()

    // Locals: method chaining on temporaries is not supported yet.
    ver_opt := flags.get("version")
    ver := ver_opt.unwrap_or("false")
    if ver == "true" {
        print("L Language v1.0.0")
    }

    name_opt := flags.get("name")
    if name_opt.is_some() {
        print("Hello, " + name_opt.unwrap())
    }
    return 0
}
