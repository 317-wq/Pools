cmake_minimum_required(VERSION 3.10)

project(MemoryPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

# 单元测试
add_executable(test
                tests/test.cpp
                src/memory_pool.cpp)
target_link_libraries(test pthread)

# 性能测试 (Benchmark)
add_executable(bench
                benchmarks/bench.cpp
                src/memory_pool.cpp)
target_link_libraries(bench pthread)

# 场景示例 (Demo)
add_executable(example
                examples/example.cpp
                src/memory_pool.cpp)
target_link_libraries(example pthread)