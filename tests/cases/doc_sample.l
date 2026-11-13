# Adds two integers.
pub fun add(i32 a, i32 b) -> i32 {
    return a + b
}

// Readable value source.
pub interface IReader {
    fun read_value() -> i32
}

# A 2D point that implements IReader.
pub class Point : IReader {
    pub i32 x
    fun init(i32 v) {
        x = v
    }
    pub fun read_value() -> i32 {
        return x
    }
}
