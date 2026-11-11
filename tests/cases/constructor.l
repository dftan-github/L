class User {
    i32 age_
    fun init(i32 a) {
        age_ = a + 10
    }
    pub fun get_age() -> i32 {
        return age_
    }
}

fun main() -> i32 {
    u := User(5)
    print(u.get_age())
    return 0
}
