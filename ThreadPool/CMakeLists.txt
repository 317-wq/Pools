cmake_minimum_required(VERSION 3.10)

project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)

add_executable(test 
                tests/test.cpp
                src/thread_pool.cpp)

target_link_libraries(test pthread)