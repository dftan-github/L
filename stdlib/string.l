// Minimal String wrapper over a C string pointer.
class String {
    ptr data_

    fun init(ptr d) {
        data_ = d
    }

    fun printSelf() -> void {
        l_print_str(data_)
    }

    pub fun concat(String other) -> String {
        ptr new_data = l_str_concat(data_, other.data_)
        return String(new_data)
    }
}
