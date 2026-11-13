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

fun check_and_print<T: IPrintable>(T item) {
    item.printSelf()
}

fun main() -> i32 {
    check_and_print(Bad())
    return 0
}
