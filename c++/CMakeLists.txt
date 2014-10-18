project("Cap'n Proto" CXX)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -std=c++11")

add_subdirectory(src)
