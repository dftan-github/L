class Bad {
    i32 x

    static fun boom() -> i32 {
        return x
    }
}

fun main() -> i32 {
    return Bad.boom()
}
