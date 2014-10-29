
# Options ======================================================================

option(BUILD_TOOLS "Build command-line tools and compiler." ON)
option(BUILD_TESTING "Build unit tests and enable CTest 'check' target." ON)
option(EXTERNAL_CAPNP "Use the system capnp binary, or the one specified in $CAPNP, instead of using the compiled one." OFF)
option(CAPNP_LITE "Compile Cap'n Proto in 'lite mode', in which all reflection APIs (schema.h, dynamic.h, etc.) are not included. Produces a smaller library at the cost of features. All programs built against the library must be compiled with -DCAPNP_LITE. Requires EXTERNAL_CAPNP to build the tests." OFF)

# Check for invalid combinations of build options
if(NOT BUILD_TOOLS AND BUILD_TESTING AND NOT EXTERNAL_CAPNP)
  # Not *all* of the tests require the capnp compiler, and those that do could be excluded
  # when not building the tools, but it's easier to just have a blanket rule.
  message(SEND_ERROR "Tests (BUILD_TESTING) cannot be build without either BUILD_TOOLS or EXTERNAL_CAPNP.")
endif()

if(CAPNP_LITE AND BUILD_TESTING AND NOT EXTERNAL_CAPNP)
  # As above, we could exclude only the tests that depend on the compiler.
  message(SEND_ERROR "CAPNP_LITE with BUILD_TESTING requires EXTERNAL_CAPNP.")
endif()

if(CAPNP_LITE AND BUILD_TOOLS)
  message(WARNING "Command-line tools will not be built with CAPNP_LITE.")
endif()

if(CAPNP_LITE)
  set(CAPNP_LITE_FLAG "-DCAPNP_LITE")
else()
  set(CAPNP_LITE_FLAG)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -std=c++11 -pthread ${CAPNP_LITE_FLAG}")

# Source =======================================================================

add_subdirectory(src)

# Install ======================================================================

# Variables for pkg-config files
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "${EXEC_INSTALL_PREFIX}")
set(libdir "${LIB_INSTALL_DIR}")
set(includedir "${INCLUDE_INSTALL_DIR}")
set(PTHREAD_CFLAGS "-pthread")
set(STDLIB_FLAG)  # TODO: Unsupported

configure_file(capnp.pc.in "${CMAKE_CURRENT_BINARY_DIR}/capnp.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/capnp.pc" DESTINATION "${LIB_INSTALL_DIR}/pkgconfig")

if(NOT CAPNP_LITE)
  configure_file(capnp-rpc.pc.in "${CMAKE_CURRENT_BINARY_DIR}/capnp-rpc.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/capnp-rpc.pc" DESTINATION "${LIB_INSTALL_DIR}/pkgconfig")
endif()

unset(STDLIB_FLAG)
unset(PTHREAD_CFLAGS)
unset(includedir)
unset(libdir)
unset(exec_prefix)
unset(prefix)
