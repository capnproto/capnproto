project(Private)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -std=c++11")

add_subdirectory(gtest)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gtest/include)
include(CTest)

add_subdirectory(src)