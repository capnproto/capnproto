cmake_minimum_required(VERSION 3.6)
project("Cap'n Proto" CXX)
set(VERSION 1.1-dev)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(CheckIncludeFileCXX)
include(GNUInstallDirs)
if(MSVC)
  check_include_file_cxx(initializer_list HAS_CXX14)
else()
  check_include_file_cxx(initializer_list HAS_CXX14 "-std=gnu++1y")
endif()
if(NOT HAS_CXX14)
  message(SEND_ERROR "Requires a C++14 compiler and standard library.")
endif()

# these arguments are passed to all install(TARGETS) calls
set(INSTALL_TARGETS_DEFAULT_ARGS
  EXPORT CapnProtoTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# Options ======================================================================

option(EXTERNAL_CAPNP "Use the system capnp binary, or the one specified in $CAPNP, instead of using the compiled one." OFF)
option(CAPNP_LITE "Compile Cap'n Proto in 'lite mode', in which all reflection APIs (schema.h, dynamic.h, etc.) are not included. Produces a smaller library at the cost of features. All programs built against the library must be compiled with -DCAPNP_LITE. Requires EXTERNAL_CAPNP." OFF)

# Check for invalid combinations of build options
if(CAPNP_LITE AND BUILD_TESTING AND NOT EXTERNAL_CAPNP)
  message(SEND_ERROR "You must set EXTERNAL_CAPNP when using CAPNP_LITE and BUILD_TESTING.")
endif()

if(CAPNP_LITE)
  set(CAPNP_LITE_FLAG "-DCAPNP_LITE")
  # This flag is attached as PUBLIC target_compile_definition to kj target
else()
  set(CAPNP_LITE_FLAG)
endif()

set(WITH_OPENSSL "AUTO" CACHE STRING
  "Whether or not to build libkj-tls by linking against openssl")
# define list of values GUI will offer for the variable
set_property(CACHE WITH_OPENSSL PROPERTY STRINGS AUTO ON OFF)

set(WITH_ZLIB "AUTO" CACHE STRING
  "Whether or not to build libkj-gzip by linking against zlib")
set_property(CACHE WITH_ZLIB PROPERTY STRINGS AUTO ON OFF)

# shadow cache variable original value with ON/OFF,
# so from now on OpenSSL-specific code just has to check:
#     if (WITH_OPENSSL)
#         ...
#     endif()
if (CAPNP_LITE)
  set(WITH_OPENSSL OFF)
elseif (WITH_OPENSSL STREQUAL "AUTO")
  find_package(OpenSSL COMPONENTS Crypto SSL)
  if (OPENSSL_FOUND)
    set(WITH_OPENSSL ON)
  else()
    set(WITH_OPENSSL OFF)
  endif()
elseif (WITH_OPENSSL)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto SSL)
endif()

# shadow cache variable original value with ON/OFF,
# so from now on ZLIB-specific code just has to check:
#     if (WITH_ZLIB)
#         ...
#     endif()
if(CAPNP_LITE)
  set(WITH_ZLIB OFF)
elseif (WITH_ZLIB STREQUAL "AUTO")
  find_package(ZLIB)
  if(ZLIB_FOUND)
    set(WITH_ZLIB ON)
  else()
    set(WITH_ZLIB OFF)
  endif()
elseif (WITH_ZLIB)
  find_package(ZLIB REQUIRED)
endif()

set(WITH_FIBERS "AUTO" CACHE STRING
  "Whether or not to build libkj-async with fibers")
# define list of values GUI will offer for the variable
set_property(CACHE WITH_FIBERS PROPERTY STRINGS AUTO ON OFF)

# CapnProtoConfig.cmake.in needs this variable.
set(_WITH_LIBUCONTEXT OFF)

if (WITH_FIBERS OR WITH_FIBERS STREQUAL "AUTO")
  set(_capnp_fibers_found OFF)
  if (WIN32 OR CYGWIN)
    set(_capnp_fibers_found ON)
  else()
    # Fibers need makecontext, setcontext, getcontext, swapcontext that may be in libc,
    # or in libucontext (e.g. for musl).
    # We assume that makecontext implies that the others are present.
    include(CheckLibraryExists)
    check_library_exists(c makecontext "" HAVE_UCONTEXT_LIBC)
    if (HAVE_UCONTEXT_LIBC)
      set(_capnp_fibers_found ON)
    else()
      # Try with libucontext
      find_package(PkgConfig)
      if (PKG_CONFIG_FOUND)
        pkg_check_modules(libucontext IMPORTED_TARGET libucontext)
        if (libucontext_FOUND)
          set(_WITH_LIBUCONTEXT ON)
          set(_capnp_fibers_found ON)
        endif()
      else()
        set(_capnp_fibers_found OFF)
      endif()
    endif()
  endif()

  if (_capnp_fibers_found)
    set(WITH_FIBERS ON)
  elseif(WITH_FIBERS STREQUAL "AUTO")
    set(WITH_FIBERS OFF)
  else()
    message(FATAL_ERROR "Missing 'makecontext', 'getcontext', 'setcontext' or 'swapcontext' symbol in libc and no libucontext found: KJ won't be able to build with fibers. Disable fibers (-DWITH_FIBERS=OFF).")
  endif()
endif()

if(MSVC)
  # TODO(cleanup): Enable higher warning level in MSVC, but make sure to test
  #   build with that warning level and clean out false positives.

  add_compile_options(/wo4503)
  # Only warn once on truncated decorated names. The maximum symbol length MSVC
  # supports is 4k characters, which the parser framework regularly blows. The
  # compiler likes to print out the entire type that went over the limit along
  # with this warning, which gets unbearably spammy. That said, we don't want to
  # just ignore it, so I'm letting it trigger once until we find some places to
  # inject ParserRefs.
else()
  # Note that it's important to add new CXXFLAGS before ones specified by the
  # user, so that the user's flags override them. This is particularly
  # important if -Werror was enabled and then certain warnings need to be
  # disabled, as is done in super-test.sh.
  #
  # We enable a lot of warnings, but then disable some:
  # * strict-aliasing: We use type-punning in known-safe ways that GCC doesn't
  #   recognize as safe.
  # * sign-compare: Low S/N ratio.
  # * unused-parameter: Low S/N ratio.
  add_compile_options(-Wall -Wextra -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-parameter)

  if(DEFINED CMAKE_CXX_EXTENSIONS AND NOT CMAKE_CXX_EXTENSIONS)
    message(SEND_ERROR "Cap'n Proto requires compiler-specific extensions (e.g., -std=gnu++14). Please leave CMAKE_CXX_EXTENSIONS undefined or ON.")
  endif()

  if (NOT ANDROID)
    add_compile_options(-pthread)
  endif()
endif()

# Source =======================================================================
include(CapnProtoMacros)
add_subdirectory(src)

# Install ======================================================================

include(CMakePackageConfigHelpers)

# We used to use write_basic_package_version_file(), but since the autotools build needs to install
# a config version script as well, I copied the AnyNewerVersion template from my CMake Modules
# directory to Cap'n Proto's cmake/ directory (alternatively, we could make the autotools build
# depend on CMake).
#
# We might as well use the local copy of the template. In the future we can modify the project's
# version compatibility policy just by changing that file.
set(PACKAGE_VERSION ${VERSION})
configure_file(cmake/CapnProtoConfigVersion.cmake.in cmake/CapnProtoConfigVersion.cmake @ONLY)

set(CONFIG_PACKAGE_LOCATION ${CMAKE_INSTALL_LIBDIR}/cmake/CapnProto)

configure_package_config_file(cmake/CapnProtoConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CapnProtoConfig.cmake
  INSTALL_DESTINATION ${CONFIG_PACKAGE_LOCATION}
  PATH_VARS CMAKE_INSTALL_FULL_INCLUDEDIR
)
export(EXPORT CapnProtoTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/CapnProtoTargets.cmake"
  NAMESPACE CapnProto::
)
install(EXPORT CapnProtoTargets
  FILE CapnProtoTargets.cmake
  NAMESPACE CapnProto::
  DESTINATION ${CONFIG_PACKAGE_LOCATION}
)
install(FILES
  cmake/CapnProtoMacros.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CapnProtoConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CapnProtoConfigVersion.cmake
  DESTINATION ${CONFIG_PACKAGE_LOCATION}
)
#install CapnProtoMacros for CapnProtoConfig.cmake build directory consumers
configure_file(cmake/CapnProtoMacros.cmake cmake/CapnProtoMacros.cmake COPYONLY)

if(NOT MSVC)  # Don't install pkg-config files when building with MSVC
  # Variables for pkg-config files
  set(prefix "${CMAKE_INSTALL_PREFIX}")
  set(exec_prefix "") # not needed since we use absolute paths in libdir and includedir
  set(libdir "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(includedir "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  set(PTHREAD_CFLAGS "-pthread")
  set(STDLIB_FLAG)  # TODO: Unsupported

  set(CAPNP_PKG_CONFIG_FILES
    pkgconfig/kj.pc
    pkgconfig/capnp.pc
    pkgconfig/capnpc.pc
  )

  if(NOT CAPNP_LITE)
    list(APPEND CAPNP_PKG_CONFIG_FILES
      pkgconfig/kj-async.pc
      pkgconfig/kj-gzip.pc
      pkgconfig/kj-http.pc
      pkgconfig/kj-test.pc
      pkgconfig/kj-tls.pc
      pkgconfig/capnp-rpc.pc
      pkgconfig/capnp-websocket.pc
      pkgconfig/capnp-json.pc
    )
  endif()

  foreach(pcfile ${CAPNP_PKG_CONFIG_FILES})
    configure_file(${pcfile}.in "${CMAKE_CURRENT_BINARY_DIR}/${pcfile}" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${pcfile}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  endforeach()

  unset(STDLIB_FLAG)
  unset(PTHREAD_CFLAGS)
  unset(includedir)
  unset(libdir)
  unset(exec_prefix)
  unset(prefix)
endif()
