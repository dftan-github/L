// 边界 4b: 私有字段绝不能被 delegate 越权访问 (编译期必须拒绝).
// 期待诊断: [ERROR] ... - unknown property 'token' on 'Holder'

class Secret {
    i32 token
    fun init() {
        token = 7
    }
}

class Holder {
    delegate Secret
}

fun main() -> i32 {
    h := Holder()
    print(h.token)
    return 0
}
