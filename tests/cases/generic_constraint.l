interface IPrintable {
    fun printSelf()
}

class Good : IPrintable {
    fun printSelf() {
        print("Good")
    }
}

class Bad {
    fun printSelf() {
        print("Bad")
    }
}

// Constraint: T must implement IPrintable.
fun check_and_print<T: IPrintable>(T item) {
    item.printSelf()
}

fun main() -> i32 {
    check_and_print(Good())
    // check_and_print(Bad())  // must be rejected at compile time
    return 0
}
