
# kj ===========================================================================

set(kj_sources
  common.c++
  units.c++
  memory.c++
  refcount.c++
  array.c++
  string.c++
  string-tree.c++
  exception.c++
  debug.c++
  arena.c++
  io.c++
  mutex.c++
  thread.c++
  main.c++
  parse/char.c++
)
set(kj_headers
  common.h
  units.h
  memory.h
  refcount.h
  array.h
  vector.h
  string.h
  string-tree.h
  exception.h
  debug.h
  arena.h
  io.h
  tuple.h
  one-of.h
  function.h
  mutex.h
  thread.h
  threadlocal.h
  time.h
  main.h
  windows-sanity.h
)
set(kj-parse_headers
  parse/common.h
  parse/char.h
)
add_library(kj ${kj_sources})
install(TARGETS kj ARCHIVE DESTINATION "${LIB_INSTALL_DIR}")
install(FILES ${kj_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj")
install(FILES ${kj-parse_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj/parse")

set(kj-async_sources
  async.c++
  async-unix.c++
  async-io.c++
)
set(kj-async_headers
  async-prelude.h
  async.h
  async-inl.h
  async-unix.h
  async-io.h
)
if(NOT CAPNP_LITE)
  add_library(kj-async ${kj-async_sources})
  target_link_libraries(kj-async kj)
  install(TARGETS kj-async ARCHIVE DESTINATION "${LIB_INSTALL_DIR}")
  install(FILES ${kj-async_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj")
endif()

# Tests ========================================================================

if(BUILD_TESTING)
  add_executable(kj-tests
    common-test.c++
    memory-test.c++
    refcount-test.c++
    array-test.c++
    string-test.c++
    string-tree-test.c++
    exception-test.c++
    debug-test.c++
    arena-test.c++
    units-test.c++
    tuple-test.c++
    one-of-test.c++
    function-test.c++
    io-test.c++
    mutex-test.c++
    threadlocal-test.c++
    threadlocal-pthread-test.c++
  )
  # TODO: Link with librt on Solaris for sched_yield
  target_link_libraries(kj-tests kj gtest gtest_main)
  add_dependencies(check kj-tests)
  add_test(NAME kj-tests-run COMMAND kj-tests)

  if(NOT CAPNP_LITE)
    add_executable(kj-heavy-tests
      async-test.c++
      async-unix-test.c++
      async-io-test.c++
      parse/common-test.c++
      parse/char-test.c++
    )
    target_link_libraries(kj-heavy-tests kj kj-async gtest gtest_main)
    add_dependencies(check kj-heavy-tests)
    add_test(NAME kj-heavy-tests-run COMMAND kj-heavy-tests)
  endif()  # NOT CAPNP_LITE
endif()  # BUILD_TESTING
