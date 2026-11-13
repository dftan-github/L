# L compiler frontend/backend (C++). Phase 1: Lexer → Parser → AST → Semantic → LLVM IR.
add_library(l_compiler STATIC
  lexer/lexer.cpp
  parser/parser.cpp
  ast/ast.cpp
  semantic/semantic.cpp
  codegen/codegen.cpp
  module/loader.cpp
  package/toml.cpp
  package/resolve.cpp
  package/config_host.cpp
  package/lconf_runner.cpp
  diagnostic/diagnostic.cpp
  comptime/interpreter.cpp
  comptime/expand.cpp
  comptime/snippet.cpp
  comptime/uid.cpp
  compile/compile.cpp
  iface/gen_interface.cpp
  doc/gen_doc.cpp
  jit/orc_jit.cpp
)

target_include_directories(l_compiler
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
)

separate_arguments(L_LLVM_DEFS NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(l_compiler PUBLIC ${L_LLVM_DEFS})
target_compile_features(l_compiler PUBLIC cxx_std_20)
target_compile_options(l_compiler PRIVATE -fno-rtti)

# Host object emission + ORC LLJIT (LLVM 22).
llvm_map_components_to_libnames(L_COMPILER_LLVM_LIBS
  core
  support
  native
  nativecodegen
  passes
  orcjit
  orcshared
  orctargetprocess
  executionengine
  runtimeDyld
  jitlink
  bitwriter
  bitreader
  irreader
  transformutils
  linker
)
target_link_libraries(l_compiler PUBLIC ${L_COMPILER_LLVM_LIBS})

add_executable(l main.cpp)
# Link l_runtime into the host so ORC AbsoluteSymbols can resolve l_print_* / l_alloc.
target_link_libraries(l PRIVATE l_compiler l_runtime)
target_compile_features(l PRIVATE cxx_std_20)
target_compile_options(l PRIVATE -fno-rtti)
add_dependencies(l l_runtime)

# Bake build-tree paths so `l -o out.exe` can find clang + l_runtime + stdlib.
set(L_CLANG_FOR_LINK "${CMAKE_CXX_COMPILER}")
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" L_BUILD_DIR_NORM)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" L_SOURCE_DIR_NORM)
file(TO_CMAKE_PATH "${L_CLANG_FOR_LINK}" L_CLANG_NORM)
# Escape backslashes for MSVC/Clang string literals on Windows.
string(REPLACE "\\" "\\\\" L_BUILD_DIR_ESC "${L_BUILD_DIR_NORM}")
string(REPLACE "\\" "\\\\" L_SOURCE_DIR_ESC "${L_SOURCE_DIR_NORM}")
string(REPLACE "\\" "\\\\" L_CLANG_ESC "${L_CLANG_NORM}")
target_compile_definitions(l PRIVATE
  "L_BUILD_DIR=\"${L_BUILD_DIR_ESC}\""
  "L_SOURCE_DIR=\"${L_SOURCE_DIR_ESC}\""
  "L_CLANG_PATH=\"${L_CLANG_ESC}\""
)
