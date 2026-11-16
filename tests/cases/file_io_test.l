fun main() -> i32 {
    path := String("test_file_io.txt")
    payload := "Hello\nL-Lang!\r\n"

    ok := File.write_all(path, payload)
    if !ok {
        print("write failed")
        return 1
    }

    res := File.read_all(path)
    if !res.is_some() {
        print("read failed")
        return 1
    }

    text := res.unwrap()
    print(text)
    return 0
}
