cmake_minimum_required(VERSION 3.16)
project(hybrid_aot LANGUAGES C CXX)

find_package(BISON 3.8 REQUIRED)
find_package(LLVM REQUIRED CONFIG)

BISON_TARGET(
    HybridParser
    ${CMAKE_CURRENT_SOURCE_DIR}/grammars/hybrid.y
    ${CMAKE_CURRENT_BINARY_DIR}/hybrid_parser.cpp
    DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/hybrid_parser.hpp
    COMPILE_FLAGS "-Wall"
)

llvm_map_components_to_libnames(HYBRID_LLVM_LIBS
    core
    asmparser
    support
    native
    nativecodegen
)

add_library(hybrid_jit
    src/jit.cpp
    src/parse_context.cpp
    ${BISON_HybridParser_OUTPUTS}
)
target_include_directories(hybrid_jit
    PUBLIC include
    PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(hybrid_jit SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_features(hybrid_jit PUBLIC cxx_std_17)
target_compile_options(hybrid_jit PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(hybrid_jit PRIVATE ${HYBRID_LLVM_LIBS})

foreach(example IN ITEMS emit_object native_functions host_class)
    add_executable(example_${example} examples/${example}.cpp)
    target_link_libraries(example_${example} PRIVATE hybrid_jit)
endforeach()

add_executable(test_hybrid_jit tests/test_jit.cpp)
target_link_libraries(test_hybrid_jit PRIVATE hybrid_jit)

enable_testing()
add_test(NAME hybrid_jit COMMAND test_hybrid_jit)
