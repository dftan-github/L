fun main() -> i32 {
    i32 sum = 0;
    i32 i = 1;
    while i <= 10 {
        sum = sum + i;
        i = i + 1;
    }
    if sum == 55 {
        print(999);
    } else {
        print(0);
    }
    return 0;
}
