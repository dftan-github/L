fun main() -> i32 {
    cmd := "echo L-Process-Test"
    res := run_command(cmd)

    if res.is_ok() {
        print("Command executed successfully.")
    } else {
        print("Command failed: " + res.unwrap_err())
    }
    return 0
}
