// [A499] Async MVP: `await` inside a loop has no single resume state yet, so
// the compiler must reject it with a located diagnostic.
import "stdlib/io.l"

async fun fake() -> i32 {
  return 1
}

async fun bad_loop() -> i32 {
  i! := 0
  while i < 3 {
    v := await fake()
    i = i + 1
  }
  return 0
}

fun main() -> i32 {
  t := bad_loop()
  return 0
}
