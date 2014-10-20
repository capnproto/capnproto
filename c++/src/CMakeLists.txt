
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(CapnProto)
set(CAPNP_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

# kj ==============================================================

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
set(kj_headers
  kj/common.h
  kj/units.h
  kj/memory.h
  kj/refcount.h
  kj/array.h
  kj/vector.h
  kj/string.h
  kj/string-tree.h
  kj/exception.h
  kj/debug.h
  kj/arena.h
  kj/io.h
  kj/tuple.h
  kj/one-of.h
  kj/function.h
  kj/mutex.h
  kj/thread.h
  kj/threadlocal.h
  kj/time.h
  kj/main.h
)
set(kj-parse_headers
  kj/parse/common.h
  kj/parse/char.h
)

add_library(kj-async
  kj/async.c++
  kj/async-unix.c++
  kj/async-io.c++
)
target_link_libraries(kj-async kj)
set(kj-async_headers
  kj/async-prelude.h
  kj/async.h
  kj/async-inl.h
  kj/async-unix.h
  kj/async-io.h
)

# capnp ==============================================================

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
target_link_libraries(capnp kj)
set(capnp_headers
  capnp/c++.capnp.h
  capnp/common.h
  capnp/blob.h
  capnp/endian.h
  capnp/layout.h
  capnp/orphan.h
  capnp/list.h
  capnp/any.h
  capnp/message.h
  capnp/capability.h
  capnp/schema.capnp.h
  capnp/schema.h
  capnp/schema-loader.h
  capnp/schema-parser.h
  capnp/dynamic.h
  capnp/pretty-print.h
  capnp/serialize.h
  capnp/serialize-async.h
  capnp/serialize-packed.h
  capnp/pointer-helpers.h
  capnp/generated-header-support.h
)

add_library(capnp-rpc
  capnp/serialize-async.c++
  capnp/capability.c++
  capnp/dynamic-capability.c++
  capnp/rpc.c++
  capnp/rpc.capnp.c++
  capnp/rpc-twoparty.c++
  capnp/rpc-twoparty.capnp.c++
  capnp/ez-rpc.c++
)
target_link_libraries(capnp-rpc kj-async kj)
set(capnp-rpc_headers
  capnp/rpc-prelude.h
  capnp/rpc.h
  capnp/rpc-twoparty.h
  capnp/rpc.capnp.h
  capnp/rpc-twoparty.capnp.h
  capnp/ez-rpc.h
)

# Tools/Compilers ==============================================================

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
target_link_libraries(capnpc capnp kj)
set(capnpc_headers
  capnp/c++.capnp
  capnp/schema.capnp
  capnp/rpc.capnp
  capnp/rpc-twoparty.capnp
)

add_executable(capnp-tool
  capnp/compiler/module-loader.c++
  capnp/compiler/capnp.c++
)
target_link_libraries(capnp-tool capnpc capnp kj)
set_target_properties(capnp-tool PROPERTIES OUTPUT_NAME capnp)

add_executable(capnpc_cpp
  capnp/compiler/capnpc-c++.c++
)
target_link_libraries(capnpc_cpp capnp kj)
set_target_properties(capnpc_cpp PROPERTIES OUTPUT_NAME capnpc-c++)

add_executable(capnpc_capnp
  capnp/compiler/capnpc-capnp.c++
)
target_link_libraries(capnpc_capnp capnp kj)
set_target_properties(capnpc_capnp PROPERTIES OUTPUT_NAME capnpc-capnp)

# Install ==============================================================

export(TARGETS capnp capnp-tool capnp-rpc capnpc capnpc_cpp capnpc_capnp kj kj-async
  FILE ${CMAKE_CURRENT_BINARY_DIR}/capnp-config.cmake
)
export(PACKAGE capnp)

install(TARGETS capnp capnp-tool capnp-rpc capnpc capnpc_cpp capnpc_capnp kj kj-async
  EXPORT capnp
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

# Symlink capnpc -> capnp
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink capnp ${CMAKE_INSTALL_PREFIX}/bin/capnpc)")

install(FILES ${kj_headers} DESTINATION include/kj)
install(FILES ${kj-async_headers} DESTINATION include/kj)
install(FILES ${kj-parse_headers} DESTINATION include/kj/parse)
install(FILES ${capnp_headers} DESTINATION include/capnp)
install(FILES ${capnp-rpc_headers} DESTINATION include/capnp)
install(FILES ${capnpc_headers} DESTINATION include/capnp)

# Tests ==============================================================

# Setup googletest build and library targets (gtest and gtest_main)
include(ExternalProject)
ExternalProject_Add(gtest_build
  URL http://googletest.googlecode.com/files/gtest-1.7.0.zip
  URL_HASH SHA1=f85f6d2481e2c6c4a18539e391aa4ea8ab0394af
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  INSTALL_COMMAND ""  # Disable install
)

ExternalProject_Get_Property(gtest_build binary_dir)

add_library(gtest UNKNOWN IMPORTED)
set_property(TARGET gtest
  PROPERTY IMPORTED_LOCATION ${binary_dir}/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a
)
add_dependencies(gtest gtest_build)

add_library(gtest_main UNKNOWN IMPORTED)
set_property(TARGET gtest_main
  PROPERTY IMPORTED_LOCATION ${binary_dir}/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a
)
add_dependencies(gtest_main gtest)

ExternalProject_Get_Property(gtest_build source_dir)
include_directories(${source_dir}/include)

set(test_capnp_files
  capnp/test.capnp
  capnp/test-import.capnp
  capnp/test-import2.capnp
)

set(CAPNP_EXECUTABLE $<TARGET_FILE:capnp-tool>)
set(CAPNPC_CXX_EXECUTABLE $<TARGET_FILE:capnpc_cpp>)
set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_capnp)
file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})

capnp_generate_cpp(test_capnp_cpp_files test_capnp_h_files ${test_capnp_files})

include_directories(${CAPNPC_OUTPUT_DIR})

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
  ${test_capnp_cpp_files}
  ${test_capnp_h_files}
)
target_link_libraries(capnp-tests
  capnpc
  capnp-rpc
  capnp
  kj-async
  kj
  gtest
  gtest_main
)

add_executable(capnp-evolution-tests capnp/compiler/evolution-test.c++)
target_link_libraries(capnp-evolution-tests capnpc capnp kj)

include(CTest)
add_test(NAME capnp-tests-run COMMAND capnp-tests)
add_test(NAME capnp-evolution-tests-run COMMAND capnp-evolution-tests)

# Sadly, we can't use the 'test' target, as that's coopted by ctest
add_custom_target(check ${CMAKE_CTEST_COMMAND} -V)
add_dependencies(check capnp-tests)
add_dependencies(check capnp-evolution-tests)
