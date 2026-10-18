cmake_minimum_required(VERSION 3.20)
project(CloverNPU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(clover_toolchain STATIC
    toolchain/src/isa.cpp
    toolchain/src/assembler.cpp
    toolchain/src/iss.cpp
)
target_include_directories(clover_toolchain PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/include"
)

add_executable(clover-as tools/clover_as.cpp)
target_link_libraries(clover-as PRIVATE clover_toolchain)

add_executable(clover-iss sim/iss/clover_iss.cpp)
target_link_libraries(clover-iss PRIVATE clover_toolchain)

add_executable(clover-objdump tools/clover_objdump.cpp)
target_link_libraries(clover-objdump PRIVATE clover_toolchain)

add_library(clover_compiler STATIC
    compiler/src/compiler.cpp
)
target_include_directories(clover_compiler PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/compiler/include"
)
target_link_libraries(clover_compiler PUBLIC clover_toolchain)

add_executable(cloverc compiler/tools/cloverc.cpp)
target_link_libraries(cloverc PRIVATE clover_compiler)

add_executable(clover_gemm_test tests/tile/test_gemm.cpp)
target_compile_definitions(clover_gemm_test PRIVATE
    CLOVER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_options(clover_gemm_test PRIVATE -ffp-contract=off)
target_link_libraries(clover_gemm_test PRIVATE clover_compiler)

enable_testing()
add_test(NAME gemm_iss COMMAND clover_gemm_test)
