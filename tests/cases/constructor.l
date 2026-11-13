class User {
    i32 age
    fun init(i32 a) {
        age = a + 10
    }
    pub fun get_age() -> i32 {
        return age
    }
}

fun main() -> i32 {
    u := User(5)
    print(u.get_age())
    return 0
}
