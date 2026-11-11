# L compiler frontend/backend (C++). Phase 1: Lexer → Parser → AST → Semantic → LLVM IR.
add_library(l_compiler STATIC
  lexer/lexer.cpp
  parser/parser.cpp
  ast/ast.cpp
  semantic/semantic.cpp
  codegen/codegen.cpp
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

llvm_map_components_to_libnames(L_COMPILER_LLVM_LIBS core support)
target_link_libraries(l_compiler PUBLIC ${L_COMPILER_LLVM_LIBS})

add_executable(l main.cpp)
target_link_libraries(l PRIVATE l_compiler)
target_compile_features(l PRIVATE cxx_std_20)
target_compile_options(l PRIVATE -fno-rtti)
