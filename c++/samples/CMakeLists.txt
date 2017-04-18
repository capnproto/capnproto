# A Cap'n Proto sample project.
#
# To build (non-MSVC):
#   1. Install Cap'n Proto somewhere ($PREFIX below):
#
#      mkdir capnproto/build
#      cd capnproto/build
#      cmake ../c++ -DCMAKE_INSTALL_PREFIX=$PREFIX
#      cmake --build . --target install
#      export PATH=$PREFIX/bin:$PATH
#
#   2. Ensure Cap'n Proto's executables are on the PATH, then build the sample project:
#
#      export PATH=$PREFIX/bin:$PATH
#      mkdir ../build-samples
#      cd ../build-samples
#      cmake ../c++/samples
#      cmake --build .
#
# Caveat for MSVC:
#
#   MSVC cannot yet build the Cap'n Proto executables, so there won't be any `$PREFIX/bin` directory
#   to put on the PATH. To work around this, you can use CMAKE_PREFIX_PATH or CMAKE_MODULE_PATH to
#   find the installed MSVC-built Cap'n Proto package, and set CAPNP_EXECUTABLE and
#   CAPNPC_CXX_EXECUTABLE to the MinGW-built executables manually.
#
#   Here's an example step 2, assuming a MinGW-built Cap'n Proto is installed at $PREFIX_MINGW and
#   an MSVC-built Cap'n Proto is installed at $PREFIX_MSVC:
#
#      mkdir ../build-samples
#      cd ../build-samples
#      cmake ../c++/samples -DCMAKE_PREFIX_PATH=$PREFIX_MSVC \
#          -DCAPNP_EXECUTABLE=$PREFIX_MINGW/bin/capnp.exe \
#          -DCAPNPC_CXX_EXECUTABLE=$PREFIX_MINGW/bin/capnpc-c++.exe
#      cmake --build .

project("Cap'n Proto Samples" CXX)
cmake_minimum_required(VERSION 3.0)

find_package(CapnProto CONFIG REQUIRED)

capnp_generate_cpp(addressbookSources addressbookHeaders addressbook.capnp)
add_executable(addressbook addressbook.c++ ${addressbookSources})
target_link_libraries(addressbook CapnProto::capnp)
target_include_directories(addressbook PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

capnp_generate_cpp(calculatorSources calculatorHeaders calculator.capnp)
add_executable(calculator-client calculator-client.c++ ${calculatorSources})
add_executable(calculator-server calculator-server.c++ ${calculatorSources})
target_link_libraries(calculator-client CapnProto::capnp-rpc)
target_link_libraries(calculator-server CapnProto::capnp-rpc)
target_include_directories(calculator-client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(calculator-server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
