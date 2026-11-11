// Minimal String wrapper over a C string pointer (no malloc yet).
class String {
    ptr data_;

    fun printSelf() -> void {
        l_print_str(data_);
    }
}
