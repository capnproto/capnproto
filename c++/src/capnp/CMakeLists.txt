
# capnp ========================================================================

set(capnp_sources_lite
  c++.capnp.c++
  blob.c++
  arena.c++
  layout.c++
  list.c++
  any.c++
  message.c++
  schema.capnp.c++
  serialize.c++
  serialize-packed.c++
)
set(capnp_sources_heavy
  schema.c++
  schema-loader.c++
  dynamic.c++
  stringify.c++
)
if(NOT CAPNP_LITE)
  set(capnp_sources ${capnp_sources_lite} ${capnp_sources_heavy})
else()
  set(capnp_sources ${capnp_sources_lite})
endif()

set(capnp_headers
  c++.capnp.h
  common.h
  blob.h
  endian.h
  layout.h
  orphan.h
  list.h
  any.h
  message.h
  capability.h
  membrane.h
  dynamic.h
  schema.h
  schema.capnp.h
  schema-lite.h
  schema-loader.h
  schema-parser.h
  pretty-print.h
  serialize.h
  serialize-async.h
  serialize-packed.h
  serialize-text.h
  pointer-helpers.h
  generated-header-support.h
  raw-schema.h
)
set(capnp_schemas
  c++.capnp
  schema.capnp
)
add_library(capnp ${capnp_sources})
add_library(CapnProto::capnp ALIAS capnp)
target_link_libraries(capnp PUBLIC kj)
#make sure external consumers don't need to manually set the include dirs
target_include_directories(capnp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  $<INSTALL_INTERFACE:include>
)
install(TARGETS capnp ${INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${capnp_headers} ${capnp_schemas} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/capnp")

set(capnp-rpc_sources
  serialize-async.c++
  capability.c++
  membrane.c++
  dynamic-capability.c++
  rpc.c++
  rpc.capnp.c++
  rpc-twoparty.c++
  rpc-twoparty.capnp.c++
  persistent.capnp.c++
  ez-rpc.c++
)
set(capnp-rpc_headers
  rpc-prelude.h
  rpc.h
  rpc-twoparty.h
  rpc.capnp.h
  rpc-twoparty.capnp.h
  persistent.capnp.h
  ez-rpc.h
)
set(capnp-rpc_schemas
  rpc.capnp
  rpc-twoparty.capnp
  persistent.capnp
)
if(NOT CAPNP_LITE)
  add_library(capnp-rpc ${capnp-rpc_sources})
  add_library(CapnProto::capnp-rpc ALIAS capnp-rpc)
  target_link_libraries(capnp-rpc PUBLIC capnp kj-async kj)
  install(TARGETS capnp-rpc ${INSTALL_TARGETS_DEFAULT_ARGS})
  install(FILES ${capnp-rpc_headers} ${capnp-rpc_schemas} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/capnp")
endif()

# capnp-json ========================================================================

set(capnp-json_sources
  compat/json.c++
  compat/json.capnp.c++
)
set(capnp-json_headers
  compat/json.h
  compat/json.capnp.h
)
set(capnp-json_schemas
  compat/json.capnp
)
if(NOT CAPNP_LITE)
  add_library(capnp-json ${capnp-json_sources})
  add_library(CapnProto::capnp-json ALIAS capnp-json)
  target_link_libraries(capnp-json PUBLIC capnp kj-async kj)
  install(TARGETS capnp-json ${INSTALL_TARGETS_DEFAULT_ARGS})
  install(FILES ${capnp-json_headers} ${capnp-json_schemas} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/capnp/compat")
endif()

# Tools/Compilers ==============================================================

set(capnpc_sources
  compiler/type-id.c++
  compiler/error-reporter.c++
  compiler/lexer.capnp.c++
  compiler/lexer.c++
  compiler/grammar.capnp.c++
  compiler/parser.c++
  compiler/node-translator.c++
  compiler/compiler.c++
  schema-parser.c++
  serialize-text.c++
)
if(NOT CAPNP_LITE)
  add_library(capnpc ${capnpc_sources})
  target_link_libraries(capnpc PUBLIC capnp kj)
  install(TARGETS capnpc ${INSTALL_TARGETS_DEFAULT_ARGS})
  install(FILES ${capnpc_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/capnp")
endif()

if(NOT CAPNP_LITE)
  add_executable(capnp_tool
    compiler/module-loader.c++
    compiler/capnp.c++
  )
  target_link_libraries(capnp_tool capnpc capnp kj)
  set_target_properties(capnp_tool PROPERTIES OUTPUT_NAME capnp)
  set_target_properties(capnp_tool PROPERTIES CAPNP_INCLUDE_DIRECTORY
    $<JOIN:$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>,$<INSTALL_INTERFACE:${CMAKE_INSTALL_BINDIR}/..>>
  )

  add_executable(capnpc_cpp
    compiler/capnpc-c++.c++
  )
  target_link_libraries(capnpc_cpp capnp kj)
  set_target_properties(capnpc_cpp PROPERTIES OUTPUT_NAME capnpc-c++)
  #Capnp tool needs capnpc_cpp location. But cmake deprecated LOCATION property.
  #So we use custom property to pass location
  set_target_properties(capnpc_cpp PROPERTIES CAPNPC_CXX_EXECUTABLE
    $<TARGET_FILE:capnpc_cpp>
  )

  add_executable(capnpc_capnp
    compiler/capnpc-capnp.c++
  )
  target_link_libraries(capnpc_capnp capnp kj)
  set_target_properties(capnpc_capnp PROPERTIES OUTPUT_NAME capnpc-capnp)

  install(TARGETS capnp_tool capnpc_cpp capnpc_capnp ${INSTALL_TARGETS_DEFAULT_ARGS})

  # Symlink capnpc -> capnp
  install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink capnp \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/capnpc\")")
endif()  # NOT CAPNP_LITE

# Tests ========================================================================

if(BUILD_TESTING)
  set(test_capnp_files
    test.capnp
    test-import.capnp
    test-import2.capnp
  )

  # Add "/capnp" to match the path used to import the files in the test sources
  set(CAPNPC_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_capnp/capnp")
  include_directories("${CMAKE_CURRENT_BINARY_DIR}/test_capnp")  # Note: no "/capnp"
  file(MAKE_DIRECTORY "${CAPNPC_OUTPUT_DIR}")

  capnp_generate_cpp(test_capnp_cpp_files test_capnp_h_files ${test_capnp_files})

  if(CAPNP_LITE)
    set(test_libraries capnp kj-test kj)
  else()
    set(test_libraries capnp-json capnp-rpc capnp capnpc kj-async kj-test kj)
  endif()

  add_executable(capnp-tests
    common-test.c++
    blob-test.c++
    endian-test.c++
    endian-fallback-test.c++
    layout-test.c++
    any-test.c++
    message-test.c++
    encoding-test.c++
    orphan-test.c++
    serialize-test.c++
    serialize-packed-test.c++
    canonicalize-test.c++
    fuzz-test.c++
    test-util.c++
    ${test_capnp_cpp_files}
    ${test_capnp_h_files}
  )
  target_link_libraries(capnp-tests ${test_libraries})
  add_dependencies(check capnp-tests)
  add_test(NAME capnp-tests-run COMMAND capnp-tests)

  if(NOT CAPNP_LITE)
    add_executable(capnp-heavy-tests
      endian-reverse-test.c++
      capability-test.c++
      membrane-test.c++
      schema-test.c++
      schema-loader-test.c++
      schema-parser-test.c++
      dynamic-test.c++
      stringify-test.c++
      serialize-async-test.c++
      serialize-text-test.c++
      rpc-test.c++
      rpc-twoparty-test.c++
      ez-rpc-test.c++
      compiler/lexer-test.c++
      compiler/type-id-test.c++
      test-util.c++
      compat/json-test.c++
      ${test_capnp_cpp_files}
      ${test_capnp_h_files}
    )
    target_link_libraries(capnp-heavy-tests ${test_libraries})
    if(NOT MSVC)
      set_target_properties(capnp-heavy-tests
        PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations"
      )
    endif()

    add_dependencies(check capnp-heavy-tests)
    add_test(NAME capnp-heavy-tests-run COMMAND capnp-heavy-tests)

    add_executable(capnp-evolution-tests compiler/evolution-test.c++)
    target_link_libraries(capnp-evolution-tests capnpc capnp kj)
    add_dependencies(check capnp-evolution-tests)
    add_test(NAME capnp-evolution-tests-run COMMAND capnp-evolution-tests)
  endif()  # NOT CAPNP_LITE
endif()  # BUILD_TESTING
