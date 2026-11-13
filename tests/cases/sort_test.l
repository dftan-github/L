fun bubble_sort(Array<i32> arr!) {
    n := arr.len()
    i! := 0
    while i < n {
        j! := 0
        while j < n - i - 1 {
            a := arr.get(j)
            b := arr.get(j + 1)
            if a > b {
                arr.set(j, b)
                arr.set(j + 1, a)
            }
            j! += 1
        }
        i! += 1
    }
}

fun main() -> i32 {
    arr := [5, 2, 9, 1, 5, 6]
    bubble_sort(arr)

    k! := 0
    while k < arr.len() {
        print(arr.get(k))
        k! += 1
    }
    return 0
}
