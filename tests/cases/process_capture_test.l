fun main() -> i32 {
    res := run_capture_stdout(String("echo hello_capture"))
    if res.is_ok() == false {
        print("fail launch")
        return 1
    }
    raw_out := res.unwrap()
    out := raw_out.trim()
    print(out)
    needle := String("hello_capture")
    if out.contains(needle) == false {
        print("fail stdout")
        return 1
    }
    print(String("capture_ok"))
    return 0
}
