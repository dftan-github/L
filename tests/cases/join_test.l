fun main() -> i32 {
    h := spawn {
        sleep(50)
        print(String("worker"))
    }
    join(h)
    print(String("joined"))
    return 0
}
