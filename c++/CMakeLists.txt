project("Cap'n Proto" CXX)
cmake_minimum_required(VERSION 2.8)
set(VERSION 0.6-dev)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckIncludeFileCXX)
if(MSVC)
  check_include_file_cxx(initializer_list HAS_CXX11)
else()
  check_include_file_cxx(initializer_list HAS_CXX11 "-std=gnu++0x")
endif()
if(NOT HAS_CXX11)
  message(SEND_ERROR "Requires a C++11 compiler and standard library.")
endif()

# Set installation paths; these can be overridden in the cache.
set(EXEC_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}"
  CACHE PATH "Base installation path for executables."
)
set(BIN_INSTALL_DIR "${EXEC_INSTALL_PREFIX}/bin"
  CACHE PATH "Installation directory for binaries (default: prefix/bin)."
)
set(LIB_INSTALL_DIR "${EXEC_INSTALL_PREFIX}/lib"
  CACHE PATH "Installation directory for libraries (default: prefix/lib)."
)
set(INCLUDE_INSTALL_DIR "${EXEC_INSTALL_PREFIX}/include"
  CACHE PATH "Installation directory for header files (default: prefix/include)."
)

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

if(MSVC AND NOT CAPNP_LITE)
  message(SEND_ERROR "Building with MSVC is only supported with CAPNP_LITE.")
endif()

if(CAPNP_LITE)
  set(CAPNP_LITE_FLAG "-DCAPNP_LITE")
else()
  set(CAPNP_LITE_FLAG)
endif()

if(MSVC)
  # TODO(cleanup): Enable higher warning level in MSVC, but make sure to test
  #   build with that warning level and clean out false positives.
  set(CMAKE_CXX_FLAGS "${CAPNP_LITE_FLAG} ${CMAKE_CXX_FLAGS}")
else()
  # Note that it's important to add new CXXFLAGS before ones specified by the
  # user, so that the user's flags override them. This is particularly
  # important if -Werror was enabled and then certain warnings need to be
  # disabled, as is done in super-test.sh.
  #
  # We enable a lot of warnings, but then disable some:
  # * strict-aliasing: We use type-punning in known-safe ways that GCC doesn't
  #   recognize as safe.
  # * sign-compare: Low S/N ratio.
  # * unused-parameter: Low S/N ratio.
  #
  # We have to use -std=gnu++0x isntead of -std=c++11 because otherwise we lose
  # GNU extensions that we need.
  set(CMAKE_CXX_FLAGS "-std=gnu++0x -Wall -Wextra -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-parameter -pthread ${CAPNP_LITE_FLAG} ${CMAKE_CXX_FLAGS}")
endif()

# Source =======================================================================

add_subdirectory(src)

# Install ======================================================================

if(NOT MSVC)  # Don't install pkg-config files when building with MSVC
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
endif()
