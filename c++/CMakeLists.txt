project(Private)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -std=c++11")

if(EXISTS "${dir}" AND IS_DIRECTORY "${dir}")
  add_subdirectory(gtest)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gtest/include)
else()
  message(FATAL_ERROR "Please use setup-gtest.sh to download gtest before running the cmake build")
endif()

include(CTest)

add_subdirectory(src)