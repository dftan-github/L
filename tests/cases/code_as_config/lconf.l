fun main() -> i32 {
    // Register project deps via host AbsoluteSymbols (C++ add_dependency).
    add_dependency("crypto", "1.0")
    add_dependency("network", "0.2")

    if get_build_mode() == "Debug" {
        add_dependency("debug_utils", "0.1")
    }
    return 0
}
