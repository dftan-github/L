cmake_minimum_required(VERSION 3.20)
project(L LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LLVM_DIR "D:/Librarys/clang+llvm-22.1.0-x86_64-pc-windows-msvc/lib/cmake/llvm" CACHE PATH "LLVM cmake dir")
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM include: ${LLVM_INCLUDE_DIRS}")

# Prebuilt LLVM CMake still links VS 2022 diaguids.lib; retarget to local VS 2026.
if(TARGET LLVMDebugInfoPDB)
  set_property(TARGET LLVMDebugInfoPDB PROPERTY INTERFACE_LINK_LIBRARIES
    "C:/Program Files/Microsoft Visual Studio/18/Insiders/DIA SDK/lib/amd64/diaguids.lib"
    LLVMBinaryFormat LLVMObject LLVMSupport LLVMDebugInfoCodeView LLVMDebugInfoMSF)
endif()

# [performance-first] Build l_runtime as LLVM bitcode so the final link can
# inline runtime helpers into L-generated code. Off by default for non-Clang
# toolchains; turn on explicitly where supported.
option(L_RUNTIME_LTO "Build l_runtime as LTO bitcode for cross-module inlining" OFF)

add_subdirectory(runtime)
add_subdirectory(compiler)
