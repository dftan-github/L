// [anchor:136-comptime-veto] A false comptime_assert stops the build before codegen.
// Negative case: a false comptime_assert must veto the build.
// Expected: [ERROR] <file>:<line>:<col> - Compile-time error: <msg>
// and NO object file is produced (the frontend stops before codegen).
comptime {
    comptime_assert(2 > 3, "two is not greater than three")
}

fun main() -> i32 {
    return 0
}
