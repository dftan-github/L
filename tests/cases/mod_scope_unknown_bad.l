// A `mod::fn` where `mod` was never imported has no export table:
// [ERROR] ... unknown module 'nope' in 'nope::add'
fun main() -> i32 {
    print(nope::add(1, 2))
    return 0
}
