
# kj ===========================================================================

set(kj_sources_lite
  array.c++
  cidr.c++
  list.c++
  common.c++
  debug.c++
  exception.c++
  io.c++
  memory.c++
  mutex.c++
  string.c++
  source-location.c++
  hash.c++
  table.c++
  thread.c++
  main.c++
  arena.c++
  test-helpers.c++
  units.c++
  encoding.c++
)
set(kj_sources_heavy
  refcount.c++
  string-tree.c++
  time.c++
  filesystem.c++
  filesystem-disk-unix.c++
  filesystem-disk-win32.c++
  parse/char.c++
)
if(NOT CAPNP_LITE)
  set(kj_sources ${kj_sources_lite} ${kj_sources_heavy})
else()
  set(kj_sources ${kj_sources_lite})
endif()

set(kj_headers
  cidr.h
  common.h
  units.h
  memory.h
  refcount.h
  array.h
  list.h
  vector.h
  string.h
  string-tree.h
  source-location.h
  hash.h
  table.h
  map.h
  encoding.h
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
  filesystem.h
  time.h
  main.h
  win32-api-version.h
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
add_library(CapnProto::kj ALIAS kj)
target_compile_features(kj PUBLIC cxx_std_17)

if(UNIX AND NOT ANDROID)
  target_link_libraries(kj PUBLIC pthread)
endif()
#make sure the lite flag propagates to all users (internal + external) of this library
target_compile_definitions(kj PUBLIC ${CAPNP_LITE_FLAG})
#make sure external consumers don't need to manually set the include dirs
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(kj PUBLIC
  $<BUILD_INTERFACE:${PARENT_DIR}>
  $<INSTALL_INTERFACE:include>
)
# Ensure the library has a version set to match autotools build
set_target_properties(kj PROPERTIES VERSION ${VERSION})
install(TARGETS kj ${INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${kj_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj")
install(FILES ${kj-parse_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj/parse")
install(FILES ${kj-std_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj/std")

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
add_library(CapnProto::kj-test ALIAS kj-test)
target_link_libraries(kj-test PUBLIC kj)
# Ensure the library has a version set to match autotools build
set_target_properties(kj-test PROPERTIES VERSION ${VERSION})
install(TARGETS kj-test ${INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${kj-test_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj")
install(FILES ${kj-test-compat_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj/compat")

set(kj-async_sources
  async.c++
  async-unix.c++
  async-win32.c++
  async-io-win32.c++
  async-io.c++
  async-io-unix.c++
  timer.c++
)
set(kj-async_headers
  async-prelude.h
  async.h
  async-inl.h
  async-unix.h
  async-win32.h
  async-io.h
  async-queue.h
  cidr.h
  timer.h
)
if(NOT CAPNP_LITE)
  add_library(kj-async ${kj-async_sources})
  add_library(CapnProto::kj-async ALIAS kj-async)
  target_link_libraries(kj-async PUBLIC kj)
  if(WITH_FIBERS)
    target_compile_definitions(kj-async PUBLIC KJ_USE_FIBERS)
    if(_WITH_LIBUCONTEXT)
      target_link_libraries(kj-async PUBLIC PkgConfig::libucontext)
    endif()
  else()
    target_compile_definitions(kj-async PUBLIC KJ_USE_FIBERS=0)
  endif()

  if(UNIX)
    # external clients of this library need to link to pthreads
    target_compile_options(kj-async INTERFACE "-pthread")
  elseif(WIN32)
    target_link_libraries(kj-async PUBLIC ws2_32)
  endif()
  # Ensure the library has a version set to match autotools build
  set_target_properties(kj-async PROPERTIES VERSION ${VERSION})
  install(TARGETS kj-async ${INSTALL_TARGETS_DEFAULT_ARGS})
  install(FILES ${kj-async_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj")
endif()

# kj-http ======================================================================

set(kj-http_sources
  compat/url.c++
  compat/http.c++
)
set(kj-http_headers
  compat/url.h
  compat/http.h
)
if(NOT CAPNP_LITE)
  add_library(kj-http ${kj-http_sources})
  add_library(CapnProto::kj-http ALIAS kj-http)
  if(WITH_ZLIB)
    target_compile_definitions(kj-http PRIVATE KJ_HAS_ZLIB)
    target_link_libraries(kj-http PUBLIC kj-async kj ZLIB::ZLIB)
  else()
    target_link_libraries(kj-http PUBLIC kj-async kj)
  endif()
  # Ensure the library has a version set to match autotools build
  set_target_properties(kj-http PROPERTIES VERSION ${VERSION})
  install(TARGETS kj-http ${INSTALL_TARGETS_DEFAULT_ARGS})
  install(FILES ${kj-http_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj/compat")
endif()

# kj-tls ======================================================================
if(WITH_OPENSSL)
  set(kj-tls_sources
    compat/readiness-io.c++
    compat/tls.c++
  )
  set(kj-tls_headers
    compat/readiness-io.h
    compat/tls.h
  )
  if(NOT CAPNP_LITE)
    add_library(kj-tls ${kj-tls_sources})
    add_library(CapnProto::kj-tls ALIAS kj-tls)
    target_link_libraries(kj-tls PUBLIC kj-async)

    target_compile_definitions(kj-tls PRIVATE KJ_HAS_OPENSSL)
    target_link_libraries(kj-tls PRIVATE OpenSSL::SSL OpenSSL::Crypto)

    # Ensure the library has a version set to match autotools build
    set_target_properties(kj-tls PROPERTIES VERSION ${VERSION})
    install(TARGETS kj-tls ${INSTALL_TARGETS_DEFAULT_ARGS})
    install(FILES ${kj-tls_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj/compat")
  endif()
endif()

# kj-gzip ======================================================================

if(WITH_ZLIB)
  set(kj-gzip_sources
    compat/gzip.c++
  )
  set(kj-gzip_headers
    compat/gzip.h
  )
  if(NOT CAPNP_LITE)
    add_library(kj-gzip ${kj-gzip_sources})
    add_library(CapnProto::kj-gzip ALIAS kj-gzip)

    target_compile_definitions(kj-gzip PRIVATE KJ_HAS_ZLIB)
    target_link_libraries(kj-gzip PUBLIC kj-async kj ZLIB::ZLIB)

    # Ensure the library has a version set to match autotools build
    set_target_properties(kj-gzip PROPERTIES VERSION ${VERSION})
    install(TARGETS kj-gzip ${INSTALL_TARGETS_DEFAULT_ARGS})
    install(FILES ${kj-gzip_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kj/compat")
  endif()
endif()

# Tests ========================================================================

if(BUILD_TESTING)
  add_executable(kj-tests
    common-test.c++
    memory-test.c++
    array-test.c++
    list-test.c++
    string-test.c++
    table-test.c++
    map-test.c++
    exception-test.c++
    # this test overrides symbolizer and has to be linked separately
    # exception-override-symbolizer-test.c++
    debug-test.c++
    io-test.c++
    mutex-test.c++
    time-test.c++
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
      async-xthread-test.c++
      async-coroutine-test.c++
      async-unix-test.c++
      async-unix-xthread-test.c++
      async-win32-test.c++
      async-win32-xthread-test.c++
      async-io-test.c++
      async-queue-test.c++
      refcount-test.c++
      string-tree-test.c++
      encoding-test.c++
      arena-test.c++
      units-test.c++
      tuple-test.c++
      one-of-test.c++
      function-test.c++
      filesystem-test.c++
      filesystem-disk-test.c++
      parse/common-test.c++
      parse/char-test.c++
      compat/url-test.c++
      compat/http-test.c++
      compat/gzip-test.c++
      compat/tls-test.c++
    )
    target_link_libraries(kj-heavy-tests kj-http kj-async kj-test kj)
    if(WITH_OPENSSL)
      target_link_libraries(kj-heavy-tests kj-tls)
      # tls-test.c++ needs to use OpenSSL directly.
      target_link_libraries(kj-heavy-tests OpenSSL::SSL OpenSSL::Crypto)
      target_compile_definitions(kj-heavy-tests PRIVATE KJ_HAS_OPENSSL)
      set_property(
        SOURCE compat/tls-test.c++
        APPEND PROPERTY COMPILE_DEFINITIONS KJ_HAS_OPENSSL
      )
    endif()
    if(WITH_ZLIB)
      target_link_libraries(kj-heavy-tests kj-gzip)
      set_property(
        SOURCE compat/gzip-test.c++
        APPEND PROPERTY COMPILE_DEFINITIONS KJ_HAS_ZLIB
      )
    endif()
    add_dependencies(check kj-heavy-tests)
    add_test(NAME kj-heavy-tests-run COMMAND kj-heavy-tests)
  endif()  # NOT CAPNP_LITE
endif()  # BUILD_TESTING
