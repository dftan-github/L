// Array<T> basic operations: pop / insert / remove / contains / index_of /
// reverse / clear, plus the empty-table and out-of-range edges.
fun main() -> i32 {
    arr := Array<i32>()

    // Empty-state edges: no crash, empty Optional, -1 index.
    p0 := arr.pop()
    if p0.is_some() {
        print(1000)
    }
    print(arr.index_of(7))
    print(arr.contains(7))

    i! := 0
    while i < 5 {
        arr.push(i * 10)
        i! = i + 1
    }
    // 0 10 20 30 40

    print(arr.contains(20))
    print(arr.contains(99))
    print(arr.index_of(30))
    print(arr.index_of(99))

    // pop from the tail
    tail := arr.pop()
    print(tail ?? -1)
    print(arr.len())

    // insert in the middle and at the end
    arr.insert(1, 15)
    arr.insert(arr.len(), 50)
    i! = 0
    while i < arr.len() {
        print(arr.get(i))
        i! = i + 1
    }

    // remove middle element, then out-of-range no-ops
    gone := arr.remove(2)
    print(gone ?? -1)
    arr.remove(99)
    print(arr.len())

    // reverse in place
    arr.reverse()
    i! = 0
    while i < arr.len() {
        print(arr.get(i))
        i! = i + 1
    }

    // clear keeps the array usable
    arr.clear()
    print(arr.len())
    arr.push(5)
    print(arr.get(0))
    return 0
}
