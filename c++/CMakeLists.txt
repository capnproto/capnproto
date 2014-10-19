
option(BUILD_TOOLS "Build command-line tools and compiler." ON)
option(BUILD_TESTING "Build unit tests and enable CTest 'check' target." ON)
option(EXTERNAL_CAPNP "Use the system capnp binary, or the one specified in $CAPNP, instead of using the compiled one." OFF)

if (NOT BUILD_TOOLS AND BUILD_TESTING AND NOT EXTERNAL_CAPNP)
  message(WARNING "Forcing BUILD_TOOLS to ON; required by BUILD_TESTING without EXTERNAL_CAPNP.")
  set(BUILD_TOOLS TRUE)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -std=c++11 -pthread")

add_subdirectory(src)
