# Pure C runtime (Layer 1). No C++. Exposes l_alloc / l_free / io / thread / net via extern "C".
add_library(l_runtime STATIC
  memory.c
  io.c
  thread.c
  callback.c
  net.c
  sys.c
  regex.c
  tls.c
  task.c
  win32.c
)

target_include_directories(l_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(l_runtime PUBLIC c_std_11)

# [performance-first] Emit LLVM bitcode instead of machine code so the final link
# can inline runtime helpers (l_alloc, l_arena_alloc, ...) into L-generated code.
# Source logic is unchanged — only the object format differs. Gated behind an
# explicit option so plain builds still produce a normal static library.
if(NOT MSVC AND L_RUNTIME_LTO)
  target_compile_options(l_runtime PRIVATE -flto -O3)
  set_target_properties(l_runtime PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# pthreads on Unix; Winsock + Schannel + Win32 GUI (user32/gdi32/imm32) on Windows.
if(WIN32)
  target_link_libraries(l_runtime PUBLIC ws2_32 secur32 user32 gdi32 imm32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(l_runtime PUBLIC Threads::Threads)
endif()
