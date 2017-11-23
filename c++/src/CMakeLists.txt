
# Tests ========================================================================

if(BUILD_TESTING)
  include(CTest)

  if(EXTERNAL_CAPNP)
    # Setup CAPNP_GENERATE_CPP for compiling test schemas
    find_package(CapnProto CONFIG QUIET)
    if(NOT CapnProto_FOUND)
      # No working installation of Cap'n Proto found, so fall back to searching the environment.
      #
      # We search for the external capnp compiler binaries via $CAPNP, $CAPNPC_CXX, and
      # find_program(). find_program() will use various paths in its search, among them
      # ${CMAKE_PREFIX_PATH}/bin and $PATH.

      if(NOT CAPNP_EXECUTABLE)
        if(DEFINED ENV{CAPNP})
          set(CAPNP_EXECUTABLE "$ENV{CAPNP}")
        else()
          find_program(CAPNP_EXECUTABLE capnp)
        endif()
      endif()

      if(NOT CAPNPC_CXX_EXECUTABLE)
        if(DEFINED ENV{CAPNPC_CXX})
          set(CAPNPC_CXX_EXECUTABLE "$ENV{CAPNPC_CXX}")
        else()
          # Also search in the same directory that `capnp` was found in
          get_filename_component(capnp_dir "${CAPNP_EXECUTABLE}" DIRECTORY)
          find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ HINTS "${capnp_dir}")
        endif()
      endif()
    endif()

    set(CAPNP_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    #TODO(someday) It would be nice to use targets instead of variables in CAPNP_GENERATE_CPP macro
  endif()

  # Sadly, we can't use the 'test' target, as that's coopted by ctest
  add_custom_target(check "${CMAKE_CTEST_COMMAND}" -V)
endif()  # BUILD_TESTING

# kj ===========================================================================

add_subdirectory(kj)

# capnp ========================================================================

add_subdirectory(capnp)
