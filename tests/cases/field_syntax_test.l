class User {
    i32 age
    String name

    fun init(i32 a, String n) {
        age = a
        name = n
    }
    // Accessor present → u.age must call get_age().
    pub fun get_age() -> i32 {
        print("Accessor called")
        return age
    }
}

class Plain {
    pub i32 score
}

fun main() -> i32 {
    u := User(20, "L")
    print(u.age)
    u.name = "Lang"

    p := Plain()
    p.score = 99
    print(p.score)

    // u.age_ = 30  // trailing _ must be a compile error
    return 0
}
