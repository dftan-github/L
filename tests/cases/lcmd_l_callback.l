pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if "make_answer" == cmd {
        return "i32 the_answer = 42;"
    }
    return ""
}

fun main() -> i32 {
    //LCMD_make_answer()
    print(the_answer)
    return 0
}
