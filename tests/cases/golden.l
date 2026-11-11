// 1. 常量与变量
const MAX_LIMIT = 100;
constd start_time = get_current_time();
size := get_file_size("data.bin");

mut_count! := 0;           // 声明可变变量
i32 age! = 18;             // 显式类型前置可变变量
mut_count! = 10;           // 修改可变变量

// 2. 函数定义
fun add(i32 a, i32 b) -> i32 {
    return a + b;
}

// 3. 类与接口
interface IPrint {
    fun printSelf();
}

partial class User : IPrint {
    String name = "L";
    i32 age := 18;
}

partial class User {
    fun printSelf() {
        print(name);
    }
}

// 4. 尾随块与泛型
fun processList<T>(Array<T> list, fun(T) -> void action) {
    // 内部逻辑
}

list := Array<i32>(3);
list.push(1);

processList(list) { |item| 
    print(item); 
}
