# A Cap'n Proto sample project.
#
# To build (non-MSVC):
#   1. Install Cap'n Proto somewhere ($PREFIX below):
#
#      mkdir capnproto/build
#      cd capnproto/build
#      cmake ../c++ -DCMAKE_INSTALL_PREFIX=$PREFIX
#      cmake --build . --target install
#
#   2. Ensure Cap'n Proto's executables are on the PATH, then build the sample project:
#
#      export PATH=$PREFIX/bin:$PATH
#      mkdir ../build-samples
#      cd ../build-samples
#      cmake ../c++/samples
#      cmake --build .

project("Cap'n Proto Samples" CXX)
cmake_minimum_required(VERSION 3.1)

find_package(CapnProto CONFIG REQUIRED)

capnp_generate_cpp(addressbookSources addressbookHeaders addressbook.capnp)
add_executable(addressbook addressbook.c++ ${addressbookSources})
target_link_libraries(addressbook PRIVATE CapnProto::capnp)
target_include_directories(addressbook PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Don't build the rpc sample if find_package() found an installation of Cap'n Proto lite.
if(TARGET CapnProto::capnp-rpc)
  capnp_generate_cpp(calculatorSources calculatorHeaders calculator.capnp)
  add_executable(calculator-client calculator-client.c++ ${calculatorSources})
  add_executable(calculator-server calculator-server.c++ ${calculatorSources})
  target_link_libraries(calculator-client PRIVATE CapnProto::capnp-rpc)
  target_link_libraries(calculator-server PRIVATE CapnProto::capnp-rpc)
  target_include_directories(calculator-client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_include_directories(calculator-server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
