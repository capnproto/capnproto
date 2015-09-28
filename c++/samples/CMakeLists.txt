cmake_minimum_required (VERSION 2.8.12)

project ("Cap'n'proto sample" CXX)

# Needed to include generated Cap'n'proto headers
set (CMAKE_INCLUDE_CURRENT_DIR ON)

# Include CMake scripts
set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/../cmake")

# Enable tests
include(CTest)

# Add "-Wall" to compiler flags
add_definitions ("-Wall")

# Find Cap'n'proto
find_package (Capnp REQUIRED)
add_definitions(${CAPNP_DEFINITIONS})

# Compile .capnp files
capnp_generate_cpp(CAPNPSRC_addressbook CAPNPHDR_addressbook "${PROJECT_SOURCE_DIR}/addressbook.capnp")
capnp_generate_cpp(CAPNPSRC_calculator CAPNPHDR_calculator "${PROJECT_SOURCE_DIR}/calculator.capnp")

# Includes
include_directories (${CAPNP_INCLUDE_DIRS})

# Targets
add_executable (addressbook
    addressbook.c++
    "${CAPNPSRC_addressbook}"
    "${CAPNPHDR_addressbook}"
)
target_link_libraries (addressbook ${CAPNP_LIBRARIES})

add_executable (calculator-client
    calculator-client.c++
    "${CAPNPSRC_calculator}"
    "${CAPNPHDR_calculator}"
)
target_link_libraries (calculator-client ${CAPNP_LIBRARIES})

add_executable (calculator-server
    calculator-server.c++
    "${CAPNPSRC_calculator}"
)
target_link_libraries (calculator-server ${CAPNP_LIBRARIES})

# Tests
enable_testing()

set (ADDRESSBOOK_EXEC "${PROJECT_BINARY_DIR}/addressbook")
configure_file (tests/addressbook1.sh.in tests/addressbook1.sh @ONLY)
configure_file (tests/addressbook2.sh.in tests/addressbook2.sh @ONLY)

set (CALCULATOR_CLIENT_EXEC "${PROJECT_BINARY_DIR}/calculator-client")
set (CALCULATOR_SERVER_EXEC "${PROJECT_BINARY_DIR}/calculator-server")
configure_file (tests/calculator.sh.in tests/calculator.sh @ONLY)

add_test (addressbook1 "${PROJECT_BINARY_DIR}/tests/addressbook1.sh")
add_test (addressbook2 "${PROJECT_BINARY_DIR}/tests/addressbook2.sh")
add_test (calculator "${PROJECT_BINARY_DIR}/tests/calculator.sh")
