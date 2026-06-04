cmake_minimum_required(VERSION 3.10)

project(MemoryPool)

set(CMAKE_CXX_STANDARD 17)

# 原始基础测试
add_executable(test
                tests/test.cpp
                src/memory_pool.cpp)
