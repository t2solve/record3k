cmake_minimum_required(VERSION 3.10)
project(TestProject)

set(CMAKE_CXX_STANDARD 11)

include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

add_executable(test_hello_world test_hello_world.cpp)
target_link_libraries(test_hello_world gtest gtest_main)

enable_testing()
add_test(NAME HelloWorldTest COMMAND test_hello_world)