
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# Tests ========================================================================

if(BUILD_TESTING)
  include(CTest)

  # Setup CAPNP_GENERATE_CPP for compiling test schemas
  find_package(CapnProto QUIET)
  set(CAPNP_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")

  # Sadly, we can't use the 'test' target, as that's coopted by ctest
  add_custom_target(check "${CMAKE_CTEST_COMMAND}" -V)
endif()  # BUILD_TESTING

# kj ===========================================================================

add_subdirectory(kj)

# capnp ========================================================================

add_subdirectory(capnp)
