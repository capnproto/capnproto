
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
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -Dgtest_force_shared_crt=1
    INSTALL_COMMAND ""  # Disable install
  )

  ExternalProject_Get_Property(gtest_build binary_dir)

  # Set platform-specific library prefix/extensions.
  if(MSVC)
    set(gtest_prefix)
    set(gtest_suffix ".lib")
  else()
    set(gtest_prefix "lib")
    set(gtest_suffix ".a")
  endif()

  add_library(gtest UNKNOWN IMPORTED)
  if(CMAKE_GENERATOR MATCHES "Visual Studio.*" OR CMAKE_GENERATOR STREQUAL Xcode)
    set_target_properties(gtest PROPERTIES IMPORTED_LOCATION_DEBUG "${binary_dir}/Debug/${gtest_prefix}gtest${gtest_suffix}")
    set_target_properties(gtest PROPERTIES IMPORTED_LOCATION_RELEASE "${binary_dir}/Release/${gtest_prefix}gtest${gtest_suffix}")
  else()
    set_target_properties(gtest PROPERTIES IMPORTED_LOCATION "${binary_dir}/${gtest_prefix}gtest${gtest_suffix}")
  endif()
  
  add_dependencies(gtest gtest_build)

  add_library(gtest_main UNKNOWN IMPORTED)
  if(CMAKE_GENERATOR MATCHES "Visual Studio.*" OR CMAKE_GENERATOR STREQUAL Xcode)
    set_target_properties(gtest_main PROPERTIES IMPORTED_LOCATION_DEBUG "${binary_dir}/Debug/${gtest_prefix}gtest_main${gtest_suffix}")
    set_target_properties(gtest_main PROPERTIES IMPORTED_LOCATION_RELEASE "${binary_dir}/Release/${gtest_prefix}gtest_main${gtest_suffix}")
  else()
    set_target_properties(gtest_main PROPERTIES IMPORTED_LOCATION "${binary_dir}/${gtest_prefix}gtest_main${gtest_suffix}")
  endif()
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
