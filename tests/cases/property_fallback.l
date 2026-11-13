class User {
    // Case A: getter/setter present → method call
    i32 score
    pub fun get_score() -> i32 {
        return score
    }
    pub fun set_score(i32 v) {
        score = v + 1
    }

    // Case B: no accessor, pub field → fallback to level_
    pub i32 level
}

fun main() -> i32 {
    u := User()

    // 1. Has setter
    u.score = 10
    print(u.score)

    // 2. Pub field fallback
    u.level = 99
    print(u.level)
    return 0
}
