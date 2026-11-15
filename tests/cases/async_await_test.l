import "stdlib/io.l"

extern module c_task {
  fun l_executor_run_all() -> i32
  fun l_task_result_i32(ptr task) -> i32
}

async fun double_it(i32 x) -> i32 {
  print("  [double_it] start")
  r := x * 2
  print("  [double_it] done")
  return r
}

async fun fetch_data() -> i32 {
  print("  [fetch_data] before await")
  v := await double_it(21)
  print("  [fetch_data] after await")
  return v + 1
}

fun main() -> i32 {
  print("main: start")
  t := fetch_data()
  print("main: spawned")

  n := l_executor_run_all()
  print("main: drained")
  print(n)

  r := l_task_result_i32(t)
  print(r)
  return 0
}
