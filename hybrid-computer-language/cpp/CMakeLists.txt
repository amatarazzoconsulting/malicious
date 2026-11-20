cmake_minimum_required(VERSION 3.16)
project(hybrid_jit LANGUAGES CXX)

add_library(hybrid_jit src/jit.cpp)
target_include_directories(hybrid_jit PUBLIC include)
target_compile_features(hybrid_jit PUBLIC cxx_std_17)
target_compile_options(hybrid_jit PRIVATE -Wall -Wextra -Wpedantic)

add_executable(example_native_functions examples/native_functions.cpp)
target_link_libraries(example_native_functions PRIVATE hybrid_jit)

add_executable(example_host_class examples/host_class.cpp)
target_link_libraries(example_host_class PRIVATE hybrid_jit)

add_executable(example_binary_roundtrip examples/binary_roundtrip.cpp)
target_link_libraries(example_binary_roundtrip PRIVATE hybrid_jit)

enable_testing()
add_executable(test_hybrid_jit tests/test_jit.cpp)
target_link_libraries(test_hybrid_jit PRIVATE hybrid_jit)
add_test(NAME hybrid_jit COMMAND test_hybrid_jit)
