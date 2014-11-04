
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# Tests ========================================================================

if(BUILD_TESTING)
  include(CTest)

  # Setup CAPNP_GENERATE_CPP for compiling test schemas
  find_package(CapnProto QUIET)
  set(CAPNP_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")

  # Setup googletest build and library targets (gtest and gtest_main)
  include(ExternalProject)
  ExternalProject_Add(gtest_build
    URL http://googletest.googlecode.com/files/gtest-1.7.0.zip
    URL_MD5 2d6ec8ccdf5c46b05ba54a9fd1d130d7
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    INSTALL_COMMAND ""  # Disable install
  )

  ExternalProject_Get_Property(gtest_build binary_dir)

  add_library(gtest UNKNOWN IMPORTED)
  set_property(TARGET gtest
    PROPERTY IMPORTED_LOCATION "${binary_dir}/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a"
  )
  add_dependencies(gtest gtest_build)

  add_library(gtest_main UNKNOWN IMPORTED)
  set_property(TARGET gtest_main
    PROPERTY IMPORTED_LOCATION "${binary_dir}/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a"
  )
  add_dependencies(gtest_main gtest)

  ExternalProject_Get_Property(gtest_build source_dir)
  include_directories("${source_dir}/include")

  # Sadly, we can't use the 'test' target, as that's coopted by ctest
  add_custom_target(check "${CMAKE_CTEST_COMMAND}" -V)
endif()  # BUILD_TESTING

# kj ===========================================================================

add_subdirectory(kj)

# capnp ========================================================================

add_subdirectory(capnp)
