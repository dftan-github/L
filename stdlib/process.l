// External process helpers (system + capture).

pub fun run_command(String cmd) -> Result<i32, String> {
    rc := l_run_command(cmd.c_str())
    // -1 means the shell could not be launched; non-zero exit is still Ok(code).
    if rc < 0 {
        return Result<i32, String>(false, 0, String("run_command failed"))
    }
    return Result<i32, String>(true, rc, String(""))
}

// Run command and capture stdout (Phase 1: stderr empty). Owns returned String.
pub fun run_capture_stdout(String cmd) -> Result<String, String> {
    raw := l_process_run_capture(cmd.c_str())
    if l_ptr_is_null(raw) != 0 {
        return Result<String, String>(false, String(""), String("run_capture failed"))
    }
    code := l_process_result_code(raw)
    ptr p = l_process_result_stdout(raw)
    // Copy before freeing the capture block.
    n := l_str_len(p)
    ptr copy = l_str_substr(p, 0, n)
    l_process_result_free(raw)
    if code < 0 {
        return Result<String, String>(false, String(copy), String("run_capture failed"))
    }
    return Result<String, String>(true, String(copy), String(""))
}
