// Proves //LCMD_disable_class_hook() skips the interpreter: if ShouldNotTrigger
// were hooked, leaked_from_disabled_hook would exist and this would link.
pub fun __on_class_parsed(String className) -> String {
    if className == "UserAuto" {
        return "pub fun print_auto_log() -> i32 { return 888; }"
    }
    if className == "ShouldNotTrigger" {
        return "pub fun leaked_from_disabled_hook() -> i32 { return 1; }"
    }
    return ""
}

//LCMD_enable_class_hook()

class UserAuto {
    i32 id
}

//LCMD_disable_class_hook()

class ShouldNotTrigger {
    i32 x
}

fun main() -> i32 {
    return leaked_from_disabled_hook()
}
