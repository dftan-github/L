i32 global_count = 0
const MAX_SIZE = 10
constd GLOBAL_VERSION = 42

fun main() -> i32 {
    global_count = global_count + 5
    global_count = global_count + 5
    print(global_count)
    print(MAX_SIZE)
    print(GLOBAL_VERSION)
    return 0
}
