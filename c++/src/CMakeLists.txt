project(Private)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -std=c++11")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(kj
  kj/common.c++
  kj/units.c++
  kj/memory.c++
  kj/refcount.c++
  kj/array.c++
  kj/string.c++
  kj/string-tree.c++
  kj/exception.c++
  kj/debug.c++
  kj/arena.c++
  kj/io.c++
  kj/mutex.c++
  kj/thread.c++
  kj/main.c++
  kj/parse/char.c++
)

add_library(kj_async
  kj/async.c++
  kj/async-unix.c++
  kj/async-io.c++
)

add_library(capnp
  capnp/c++.capnp.c++
  capnp/blob.c++
  capnp/arena.c++
  capnp/layout.c++
  capnp/list.c++
  capnp/any.c++
  capnp/message.c++
  capnp/schema.capnp.c++
  capnp/schema.c++
  capnp/schema-loader.c++
  capnp/dynamic.c++
  capnp/stringify.c++
  capnp/serialize.c++
  capnp/serialize-packed.c++
)

add_library(capnp_rpc
  capnp/serialize-async.c++
  capnp/capability.c++
  capnp/dynamic-capability.c++
  capnp/rpc.c++
  capnp/rpc.capnp.c++
  capnp/rpc-twoparty.c++
  capnp/rpc-twoparty.capnp.c++
  capnp/ez-rpc.c++
)

add_library(capnpc
  capnp/compiler/md5.c++
  capnp/compiler/error-reporter.c++
  capnp/compiler/lexer.capnp.c++
  capnp/compiler/lexer.c++
  capnp/compiler/grammar.capnp.c++
  capnp/compiler/parser.c++
  capnp/compiler/node-translator.c++
  capnp/compiler/compiler.c++
  capnp/schema-parser.c++
)

add_executable(capnp-tool
  capnp/compiler/module-loader.c++
  capnp/compiler/capnp.c++
)
target_link_libraries(capnp-tool capnpc capnp kj)

add_executable(capnp-c++
  capnp/compiler/capnpc-c++.c++
)
target_link_libraries(capnp-c++ capnp kj)
get_property(capnp-c++_location TARGET capnp-c++ PROPERTY LOCATION)

set(test_capnp_files
  capnp/test.capnp
  capnp/test-import.capnp
  capnp/test-import2.capnp
)

set(test_capnp_cpp_files)
set(test_capnp_header_files)
set(test_capnp_capnp_files)

foreach(_file ${test_capnp_files})
  list(APPEND test_capnp_cpp_files ${CMAKE_CURRENT_BINARY_DIR}/${_file}.c++)
  list(APPEND test_capnp_header_files ${CMAKE_CURRENT_BINARY_DIR}/${_file}.h)
  get_filename_component(_capnp_abs ${_file} ABSOLUTE)
  list(APPEND test_capnp_capnp_files ${_capnp_abs})
  set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/${_file}.capnp.c++
    PROPERTIES GENERATED TRUE)
endforeach()

add_custom_command(
  OUTPUT
    ${test_capnp_cpp_files}
    ${test_capnp_header_files}
  COMMAND capnp-tool compile
    -o ${capnp-c++_location}
    --src-prefix=${CMAKE_CURRENT_SOURCE_DIR}
    -I${CMAKE_CURRENT_SOURCE_DIR}
    ${test_capnp_capnp_files}
  DEPENDS capnp-tool capnp-c++ ${test_capnp_files})
add_library(capnp_test_lib ${test_capnp_cpp_files})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(capnp-tests
  kj/common-test.c++
  kj/memory-test.c++
  kj/refcount-test.c++
  kj/array-test.c++
  kj/string-test.c++
  kj/string-tree-test.c++
  kj/exception-test.c++
  kj/debug-test.c++
  kj/arena-test.c++
  kj/units-test.c++
  kj/tuple-test.c++
  kj/one-of-test.c++
  kj/function-test.c++
  kj/io-test.c++
  kj/mutex-test.c++
  kj/threadlocal-test.c++
  kj/threadlocal-pthread-test.c++
  kj/async-test.c++
  kj/async-unix-test.c++
  kj/async-io-test.c++
  kj/parse/common-test.c++
  kj/parse/char-test.c++
  capnp/common-test.c++
  capnp/blob-test.c++
  capnp/endian-test.c++
  capnp/endian-fallback-test.c++
  capnp/endian-reverse-test.c++
  capnp/layout-test.c++
  capnp/any-test.c++
  capnp/message-test.c++
  capnp/capability-test.c++
  capnp/schema-test.c++
  capnp/schema-loader-test.c++
  capnp/dynamic-test.c++
  capnp/stringify-test.c++
  capnp/encoding-test.c++
  capnp/orphan-test.c++
  capnp/serialize-test.c++
  capnp/serialize-async-test.c++
  capnp/serialize-packed-test.c++
  capnp/rpc-test.c++
  capnp/rpc-twoparty-test.c++
  capnp/ez-rpc-test.c++
  capnp/test-util.c++
  capnp/compiler/lexer-test.c++
  capnp/compiler/md5-test.c++
)
target_link_libraries(capnp-tests
  capnp_test_lib
  capnpc
  capnp_rpc
  capnp
  kj_async
  kj
  gtest
  gtest_main
)

add_executable(capnp-evolution-tests capnp/compiler/evolution-test.c++)
target_link_libraries(capnp-evolution-tests
  capnp_test_lib
  capnpc
  capnp_rpc
  capnp
  kj_async
  kj
  gtest
  gtest_main
)

add_test(NAME capnp-tests-run COMMAND capnp-tests)
add_test(NAME capnp-evolution-tests-run COMMAND capnp-evolution-tests)

# Sadly, we can't use the 'test' target, as that's coopted by ctest
add_custom_target(check ${CMAKE_CTEST_COMMAND} -V)
add_dependencies(check capnp-tests)
add_dependencies(check capnp-evolution-tests)
