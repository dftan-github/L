// Layer 1 C runtime I/O + string helpers (linked from runtime/io.c).
extern module c_io {
    fun l_print_int(i32 val) -> void
    fun l_print_i64(i64 val) -> void
    fun l_print_f64(f64 val) -> void
    fun l_print_bool(i32 val) -> void
    fun l_print_str(ptr s) -> void
    // Fatal runtime error: prints the message and aborts the process.
    fun l_panic(ptr message) -> void
    fun l_read_line() -> ptr
    fun l_str_concat(ptr a, ptr b) -> ptr
    fun l_int_to_str(i32 val) -> ptr
    fun l_i64_to_str(i64 val) -> ptr
    fun l_f64_to_str(f64 val) -> ptr
    fun l_bool_to_str(i32 val) -> ptr
    fun l_str_from_byte(i32 c) -> ptr
    fun l_hash_string(ptr s) -> i64
    fun l_hash_to_bucket(i64 hash, i32 capacity) -> i32
    fun l_str_eq(ptr a, ptr b) -> i32
    fun l_str_len(ptr s) -> i32
    fun l_str_char_at(ptr s, i32 i) -> i32
    fun l_str_substr(ptr s, i32 start, i32 len) -> ptr
    fun l_str_to_i32(ptr s) -> i32
    fun l_ptr_is_null(ptr p) -> i32
    fun l_file_open(ptr path, ptr mode) -> ptr
    fun l_file_read(ptr file, ptr buffer, i32 size) -> i32
    fun l_file_write(ptr file, ptr buffer, i32 size) -> i32
    fun l_file_close(ptr file) -> void
    fun l_file_size(ptr file) -> i32
    // Captured at main entry so get_args() works even without importing env.l.
    fun l_init_args(i32 argc, ptr argv) -> void
    fun l_get_arg_count() -> i32
    fun l_get_arg(i32 index) -> ptr
    // getenv wrapper; NULL when unset (check with l_ptr_is_null).
    fun l_get_env(ptr name) -> ptr
    // Filesystem helpers (runtime/sys.c).
    fun l_file_exists(ptr path) -> i32
    fun l_create_dir(ptr path) -> i32
    fun l_remove_file(ptr path) -> i32
    fun l_remove_dir(ptr path) -> i32
    // Wall-clock time (runtime/sys.c). Sleep stays in thread.l → l_sleep_ms.
    fun l_time_now_sec() -> i64
    fun l_time_now_ms() -> i64
    fun l_format_time(i64 timestamp, ptr format_str) -> ptr
    // Shell command (runtime/sys.c → system()).
    fun l_run_command(ptr cmd) -> i32
    fun l_rename_path(ptr from, ptr to) -> i32
    fun l_copy_file(ptr from, ptr to) -> i32
    fun l_is_dir(ptr path) -> i32
    fun l_read_dir(ptr path) -> ptr
    fun l_process_run_capture(ptr cmd) -> ptr
    fun l_process_result_code(ptr result) -> i32
    fun l_process_result_stdout(ptr result) -> ptr
    fun l_process_result_stderr(ptr result) -> ptr
    fun l_process_result_free(ptr result) -> void
    fun l_install_shutdown_handler() -> void
    fun l_is_running() -> i32
    fun l_request_shutdown() -> void
}
