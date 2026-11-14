fun main() -> i32 {
    opt := Optional<i32>(false, 0)
    val := opt ?? 42
    print(val)

    some := Optional<i32>(true, 7)
    print(some ?? 99)

    String blank = ""
    empty_name := Optional<String>(false, blank)
    print(empty_name ?? "guest")

    String admin = "admin"
    role := Optional<String>(true, admin)
    print(role ?? "guest")
    return 0
}
