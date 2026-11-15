// Incremental-compilation probe: a tiny program whose source text can be
// rewritten by the Ruby test to prove the build cache is keyed on content.
fun main() -> i32 {
    print(42)
    return 0
}
