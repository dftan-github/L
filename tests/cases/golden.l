// 1. 模块级：只用 const / constd / 显式类型（:= 仅限函数内局部推导）
const MAX_LIMIT = 100
constd start_time = get_current_time()
constd size = get_file_size("data.bin")

i32 mut_count! = 0        // 模块级可变：显式类型 + !
i32 age! = 18
mut_count! = 10

// 2. 函数定义
fun add(i32 a, i32 b) -> i32 {
    return a + b
}

// 3. 类与接口（字段禁止 :=）
interface IPrint {
    fun printSelf()
}

partial class User : IPrint {
    String name = "L"
    i32 age = 18
}

partial class User {
    fun printSelf() {
        print(name)
    }
}

// 4. 尾随块与泛型（必须带接口约束，禁止裸 <T>）
fun processList<T: IPrint>(Array<T> list, fun(T) -> void action) {
    // 内部逻辑
}

// 5. Phase 1 入口；:= 仅出现在局部作用域
fun main() -> i32 {
    list! := Array<User>(1)
    u := User()
    list.push(u)

    processList(list) { |item|
        item.printSelf()
    };  // trailing-block expr stmt: explicit ';' required (no ASI after '}')

    sum := add(1, 2)
    return sum
}
