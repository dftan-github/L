pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if cmd == "gen_var" {
        id := uid_counter()
        return "i32 temp_" + id + " = 100;"
    }
    return ""
}

fun main() -> i32 {
    //LCMD_gen_var()
    //LCMD_gen_var()

    // Expect compile-time temps temp_0 and temp_1 → 200
    print(temp_0 + temp_1)
    return 0
}
