
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

# CMake support functions ======================================================

function(kj_add_library target)
  # Wrapper around `add_library()` to implement certain boilerplate practices:
  #
  #   1. The project version is appended to the library's output name (if it's a shared library).
  #   2. The library gets an ALIAS target in the CapnProto:: namespace.
  #   3. The library is install()ed using the value of INSTALL_TARGETS_DEFAULT_ARGS as arguments.
  #   4. The installed library is copied to a convenience library without the name mangling from
  #      (1) (if it's a shared library).
  #
  # You should use this function as you would `add_library()`.
  #
  # DETAILS
  #
  # The Cap'n Proto C++ ABI stability policy assumes no two versions are ABI-compatible. To prevent
  # accidental dynamic linkage to an ABI-incompatible version of the library, the project version
  # (-dev metadata and all) is appended to the library target's output name. For example, target
  # `kj` produces a library whose filename and soname are `libkj-0.7-dev.so`. Additionally, the
  # autotools build produces a `libkj.so` convenience file (but with the full soname).
  #
  # The autotools build implements this name mangling with `libtool -release`. The CMake build can
  # emulate its behavior by manually setting the OUTPUT_NAME property. Copying the file to a
  # filename without the version appendage is accomplished with a technique I learned from reading
  # the Xerces-C++ project's CMakeLists.txt, by Roger Leigh:
  # http://svn.apache.org/viewvc/xerces/c/trunk/src/CMakeLists.txt?revision=1800911&view=markup#l1269

  add_library(${target} ${ARGN})
  # Forward all arguments directly to add_library.

  add_library(CapnProto::${target} ALIAS ${target})

  install(TARGETS ${target} ${INSTALL_TARGETS_DEFAULT_ARGS})
  # This install line must precede the shared library copy script install line below to ensure the
  # library is installed before the script is executed.

  get_target_property(target_type ${target} TYPE)
  if(target_type STREQUAL "SHARED_LIBRARY")
    # The autotools build only mangles shared library names.
    set_target_properties(${target}
        PROPERTIES
          OUTPUT_NAME ${target}-${VERSION}
          INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
          # The autotools build sets the rpath this way on all libraries with dependencies
    )
    # Mangle shared library output names the same way the autotools build does.

    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/install-${target}-copy.cmake"
        CONTENT "execute_process(COMMAND ${CMAKE_COMMAND} -E copy \
\"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_LIBDIR}/$<TARGET_FILE_NAME:${target}>\" \
\"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}${target}${CMAKE_SHARED_LIBRARY_SUFFIX}\")")
    # This says: copy the installed target filename to lib${target}.so.

    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-${target}-copy.cmake")
    # We have to create a script file because we can't use generator expressions inside
    # install(CODE). This is a requested feature of CMake:
    #   https://gitlab.kitware.com/cmake/cmake/issues/15785
  endif()
endfunction()

# kj ===========================================================================

add_subdirectory(kj)

# capnp ========================================================================

add_subdirectory(capnp)
