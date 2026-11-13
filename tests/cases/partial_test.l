partial class User {
    i32 id
    fun get_id() -> i32 {
        return id
    }
}

partial class User {
    String name
    fun init(i32 i, String n) {
        id = i
        name = n
    }
}

fun main() -> i32 {
    u := User(7, "Alice")
    print(u.get_id())
    print(u.name)
    return 0
}
