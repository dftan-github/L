// [anchor:135-line-preserved] magic file/line/func names fold to literals, no runtime call.
fun test_context() {
    print("File: " + __FILE__)
    print("Line: " + __LINE__)
    print("Func: " + __FUNC__)
}

fun main() -> i32 {
    test_context()
    return 0
}
