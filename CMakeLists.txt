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

add_executable(cloverc
    compiler/tools/cloverc.cpp
    compiler/src/compiler.cpp
)
target_include_directories(cloverc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compiler/include"
)
target_link_libraries(cloverc PRIVATE clover_toolchain)

enable_testing()
