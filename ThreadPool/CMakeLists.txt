cmake_minimum_required(VERSION 3.10)

project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g")

# 原始基础测试
add_executable(test
                tests/test.cpp
                src/thread_pool.cpp)

# TPS/QPS 压力测试
add_executable(stress_test
                tests/stress_test.cpp
                src/thread_pool.cpp)

target_link_libraries(test pthread)
target_link_libraries(stress_test pthread)
