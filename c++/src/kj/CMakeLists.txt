
# kj ===========================================================================

set(kj_sources_lite
  array.c++
  common.c++
  debug.c++
  exception.c++
  io.c++
  memory.c++
  mutex.c++
  string.c++
  thread.c++
)
set(kj_sources_heavy
  units.c++
  refcount.c++
  string-tree.c++
  arena.c++
  main.c++
  parse/char.c++
)
if(NOT CAPNP_LITE)
  set(kj_sources ${kj_sources_lite} ${kj_sources_heavy})
else()
  set(kj_sources ${kj_sources_lite})
endif()

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
  miniposix.h
  io.h
  tuple.h
  one-of.h
  function.h
  mutex.h
  thread.h
  threadlocal.h
  main.h
  windows-sanity.h
)
set(kj-parse_headers
  parse/common.h
  parse/char.h
)
set(kj-std_headers
  std/iostream.h
)
add_library(kj ${kj_sources})
install(TARGETS kj ARCHIVE DESTINATION "${LIB_INSTALL_DIR}")
install(FILES ${kj_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj")
install(FILES ${kj-parse_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj/parse")
install(FILES ${kj-std_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj/std")

set(kj-test_sources
  test.c++
)
set(kj-test_headers
  test.h
)
set(kj-test-compat_headers
  compat/gtest.h
)
add_library(kj-test ${kj-test_sources})
target_link_libraries(kj-test kj)
install(TARGETS kj-test ARCHIVE DESTINATION "${LIB_INSTALL_DIR}")
install(FILES ${kj-test_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj")
install(FILES ${kj-test-compat_headers} DESTINATION "${INCLUDE_INSTALL_DIR}/kj/compat")

set(kj-async_sources
  async.c++
  async-unix.c++
  async-io.c++
  time.c++
)
set(kj-async_headers
  async-prelude.h
  async.h
  async-inl.h
  async-unix.h
  async-io.h
  time.h
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
    array-test.c++
    string-test.c++
    exception-test.c++
    debug-test.c++
    io-test.c++
    mutex-test.c++
    threadlocal-test.c++
    test-test.c++
    std/iostream-test.c++
  )
  # TODO: Link with librt on Solaris for sched_yield
  target_link_libraries(kj-tests kj-test kj)
  add_dependencies(check kj-tests)
  add_test(NAME kj-tests-run COMMAND kj-tests)

  if(NOT CAPNP_LITE)
    add_executable(kj-heavy-tests
      async-test.c++
      async-unix-test.c++
      async-io-test.c++
      refcount-test.c++
      string-tree-test.c++
      arena-test.c++
      units-test.c++
      tuple-test.c++
      one-of-test.c++
      function-test.c++
      threadlocal-pthread-test.c++
      parse/common-test.c++
      parse/char-test.c++
    )
    target_link_libraries(kj-heavy-tests kj-async kj-test kj)
    add_dependencies(check kj-heavy-tests)
    add_test(NAME kj-heavy-tests-run COMMAND kj-heavy-tests)
  endif()  # NOT CAPNP_LITE
endif()  # BUILD_TESTING
