pub fun __on_class_parsed(String className) -> String {
    if className == "UserAuto" {
        return "pub fun print_auto_log() -> i32 { return 888; }"
    }
    return ""
}

// 开启 Hook
//LCMD_enable_class_hook()

class UserAuto {
    i32 id
}

// 关闭 Hook
//LCMD_disable_class_hook()

class ShouldNotTrigger {
    i32 x
}

fun main() -> i32 {
    print(print_auto_log())
    return 0
}
