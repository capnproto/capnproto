project(Private)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -std=c++11")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gtest" AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gtest")
  add_subdirectory(gtest)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gtest/include)
else()
  message(FATAL_ERROR "Please use setup-cmake.sh to download gtest before running the cmake build")
endif()

include(CTest)

add_subdirectory(src)
