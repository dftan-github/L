pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if "User.generate" == path && "make_hello" == cmd {
        return "pub fun say_hello() -> i32 { return 42; }"
    }
    return ""
}

class User {
    i32 id
    fun generate() {
        //LCMD_make_hello()
    }
}

fun main() -> i32 {
    u := User()
    print(u.say_hello())
    return 0
}
