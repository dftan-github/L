# Pure C runtime (Layer 1). No C++. Exposes l_alloc / l_free / io / thread via extern "C".
add_library(l_runtime STATIC
  memory.c
  io.c
  thread.c
)

target_include_directories(l_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(l_runtime PUBLIC c_std_11)
