
# Tests ========================================================================

if(BUILD_TESTING)
  include(CTest)

  if (EXTERNAL_CAPNP)
    # Setup CAPNP_GENERATE_CPP for compiling test schemas
    find_package(CapnProto CONFIG QUIET)
    if (NOT CapnProto_FOUND)
      # Try and find the executables from an autotools-based installation
      # Setup paths to the schema compiler for generating ${test_capnp_files}
      if(NOT EXTERNAL_CAPNP AND NOT CAPNP_LITE)
        set(CAPNP_EXECUTABLE $<TARGET_FILE:capnp_tool>)
        set(CAPNPC_CXX_EXECUTABLE $<TARGET_FILE:capnpc_cpp>)
      else()
        # Allow paths to tools to be set with either environment variables or find_program()
        if (NOT CAPNP_EXECUTABLE)
            if (DEFINED ENV{CAPNP})
            set(CAPNP_EXECUTABLE "$ENV{CAPNP}")
            else()
            find_program(CAPNP_EXECUTABLE capnp)
            endif()
        endif()

        if(NOT CAPNPC_CXX_EXECUTABLE)
          if (DEFINED ENV{CAPNPC_CXX})
            set(CAPNPC_CXX_EXECUTABLE "$ENV{CAPNPC_CXX}")
          else()
            # Also search in the same directory that `capnp` was found in
            get_filename_component(capnp_dir "${CAPNP_EXECUTABLE}" DIRECTORY)
            find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ HINTS "${capnp_dir}")
          endif()
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
