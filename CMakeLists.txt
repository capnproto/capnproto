cmake_minimum_required(VERSION 3.10)
project("Cap'n Proto Root" CXX)

# PROJECT_IS_TOP_LEVEL was added in CMake 3.21
if (NOT DEFINED PROJECT_IS_TOP_LEVEL OR PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()

add_subdirectory(c++)
