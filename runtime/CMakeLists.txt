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
)

target_include_directories(l_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(l_runtime PUBLIC c_std_11)

# pthreads on Unix; Winsock + Schannel on Windows.
if(WIN32)
  target_link_libraries(l_runtime PUBLIC ws2_32 secur32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(l_runtime PUBLIC Threads::Threads)
endif()
