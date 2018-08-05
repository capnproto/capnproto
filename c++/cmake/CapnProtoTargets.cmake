# This CMake script adds imported targets for each shared library and executable distributed by
# Cap'n Proto's autotools build.
#
# This file IS NOT USED by the CMake build! The CMake build generates its own version of this script
# from its set of exported targets. I used such a generated script as a reference when writing this
# one.
#
# The set of library targets provided by this script is automatically generated from the list of .pc
# files maintained in configure.ac. The set of executable targets is hard-coded in this file.
#
# You can request that this script print debugging information by invoking cmake with:
#
#   -DCapnProto_DEBUG=ON
#
# TODO(someday): Distinguish between debug and release builds. I.e., set IMPORTED_LOCATION_RELEASE
#   rather than IMPORTED_LOCATION, etc., if this installation was configured as a release build. But
#   how do we tell? grep for -g in CXXFLAGS?

if(CMAKE_VERSION VERSION_LESS 3.1)
  message(FATAL_ERROR "CMake >= 3.1 required")
endif()

set(forwarded_config_flags)
if(CapnProto_FIND_QUIETLY)
  list(APPEND forwarded_config_flags QUIET)
endif()
if(CapnProto_FIND_REQUIRED)
  list(APPEND forwarded_config_flags REQUIRED)
endif()
# If the consuming project called find_package(CapnProto) with the QUIET or REQUIRED flags, forward
# them to calls to find_package(PkgConfig) and pkg_check_modules(). Note that find_dependency()
# would do this for us in the former case, but there is no such forwarding wrapper for
# pkg_check_modules().

find_package(PkgConfig ${forwarded_config_flags})
if(NOT ${PkgConfig_FOUND})
  # If we're here, the REQUIRED flag must not have been passed, else we would have had a fatal
  # error. Nevertheless, a diagnostic for this case is probably nice.
  if(NOT CapnProto_FIND_QUIETLY)
    message(WARNING "pkg-config cannot be found")
  endif()
  set(CapnProto_FOUND OFF)
  return()
endif()

function(_capnp_import_pkg_config_target target)
  # Add an imported library target named CapnProto::${target}, using the output of various
  # invocations of `pkg-config ${target}`. The generated imported library target tries to mimic the
  # behavior of a real CMake-generated imported target as closely as possible.
  #
  # Usage: _capnp_import_pkg_config_target(target <all Cap'n Proto targets>)

  set(all_targets ${ARGN})

  pkg_check_modules(${target} ${forwarded_config_flags} ${target})

  if(NOT ${${target}_FOUND})
    if(NOT CapnProto_FIND_QUIETLY)
      message(WARNING "CapnProtoConfig.cmake was configured to search for ${target}.pc, but pkg-config cannot find it. Ignoring this target.")
    endif()
    return()
  endif()

  if(CapnProto_DEBUG)
    # Dump the information pkg-config discovered.
    foreach(var VERSION LIBRARY_DIRS LIBRARIES LDFLAGS_OTHER INCLUDE_DIRS CFLAGS_OTHER)
      message(STATUS "${target}_${var} = ${${target}_${var}}")
    endforeach()
  endif()

  if(NOT ${${target}_VERSION} VERSION_EQUAL ${CapnProto_VERSION})
    if(NOT CapnProto_FIND_QUIETLY)
      message(WARNING "CapnProtoConfig.cmake was configured to search for version ${CapnProto_VERSION}, but ${target} version ${${target}_VERSION} was found. Ignoring this target.")
    endif()
    return()
  endif()

  # Make an educated guess as to what the target's .so and .a filenames must be.
  set(target_name_shared
      ${CMAKE_SHARED_LIBRARY_PREFIX}${target}-${CapnProto_VERSION}${CMAKE_SHARED_LIBRARY_SUFFIX})
  set(target_name_static
      ${CMAKE_STATIC_LIBRARY_PREFIX}${target}${CMAKE_STATIC_LIBRARY_SUFFIX})

  # Find the actual target's file. find_library() sets a cache variable, so I made the variable name
  # unique-ish.
  find_library(CapnProto_${target}_IMPORTED_LOCATION
    NAMES ${target_name_shared} ${target_name_static}  # prefer libfoo-version.so over libfoo.a
    PATHS ${${target}_LIBRARY_DIRS}
    NO_DEFAULT_PATH
  )
  # If the installed version of Cap'n Proto is in a system location, pkg-config will not have filled
  # in ${target}_LIBRARY_DIRS. To account for this, fall back to a regular search.
  find_library(CapnProto_${target}_IMPORTED_LOCATION
    NAMES ${target_name_shared} ${target_name_static}  # prefer libfoo-version.so over libfoo.a
  )

  if(NOT CapnProto_${target}_IMPORTED_LOCATION)
    # Not an error if the library doesn't exist -- we may have found a lite mode installation.
    if(CapnProto_DEBUG)
      message(STATUS "${target} library does not exist")
    endif()
    return()
  endif()

  # Record some information about this target -- shared versus static, location and soname -- which
  # we'll use to build our imported target later.

  set(target_location ${CapnProto_${target}_IMPORTED_LOCATION})
  get_filename_component(target_name "${target_location}" NAME)

  set(target_type STATIC)
  set(imported_soname_property)
  if(target_name STREQUAL ${target_name_shared})
    set(target_type SHARED)
    set(imported_soname_property IMPORTED_SONAME ${target_name})
  endif()

  # Each library dependency of the target is either the target itself, a sibling Cap'n Proto
  # library, or a system library. We ignore the first case by removing this target from the
  # dependencies. The remaining dependencies are either passed through or, if they are a sibling
  # Cap'n Proto library, prefixed with `CapnProto::`.
  set(dependencies ${${target}_LIBRARIES})
  list(REMOVE_ITEM dependencies ${target})
  set(target_interface_libs)
  foreach(dependency ${dependencies})
    list(FIND all_targets ${dependency} target_index)
    # TODO(cleanup): CMake >= 3.3 lets us write: `if(NOT ${dependency} IN_LIST all_targets)`
    if(target_index EQUAL -1)
      list(APPEND target_interface_libs ${dependency})
    else()
      list(APPEND target_interface_libs CapnProto::${dependency})
    endif()
  endforeach()

  add_library(CapnProto::${target} ${target_type} IMPORTED)
  set_target_properties(CapnProto::${target} PROPERTIES
    ${imported_soname_property}
    IMPORTED_LOCATION "${target_location}"
    # TODO(cleanup): Use cxx_std_14 once it's safe to require cmake 3.8.
    INTERFACE_COMPILE_FEATURES "cxx_generic_lambdas"
    INTERFACE_COMPILE_OPTIONS "${${target}_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${${target}_INCLUDE_DIRS}"

    # I'm dumping LDFLAGS_OTHER in with the libraries because there exists no
    # INTERFACE_LINK_OPTIONS. See https://gitlab.kitware.com/cmake/cmake/issues/16543.
    INTERFACE_LINK_LIBRARIES "${target_interface_libs};${${target}_LDFLAGS_OTHER}"
  )

  if(CapnProto_DEBUG)
    # Dump all the properties we generated for the imported target.
    foreach(prop
        IMPORTED_LOCATION
        IMPORTED_SONAME
        INTERFACE_COMPILE_FEATURES
        INTERFACE_COMPILE_OPTIONS
        INTERFACE_INCLUDE_DIRECTORIES
        INTERFACE_LINK_LIBRARIES)
      get_target_property(value CapnProto::${target} ${prop})
      message(STATUS "CapnProto::${target} ${prop} = ${value}")
    endforeach()
  endif()
endfunction()

# ========================================================================================
# Imported library targets

# Build a list of targets to search for from the list of .pc files.
# I.e. [somewhere/foo.pc, somewhere/bar.pc] -> [foo, bar]
set(library_targets)
foreach(filename ${CAPNP_PKG_CONFIG_FILES})
  get_filename_component(target ${filename} NAME_WE)
  list(APPEND library_targets ${target})
endforeach()

# Try to add an imported library target CapnProto::foo for each foo.pc distributed with Cap'n Proto.
foreach(target ${library_targets})
  _capnp_import_pkg_config_target(${target} ${library_targets})
endforeach()

# Handle lite-mode and no libraries found cases. It is tempting to set a CapnProto_LITE variable
# here, but the real CMake-generated implementation does no such thing -- we'd need to set it in
# CapnProtoConfig.cmake.in itself.
if(TARGET CapnProto::capnp AND TARGET CapnProto::kj)
  if(NOT TARGET CapnProto::capnp-rpc)
    if(NOT CapnProto_FIND_QUIETLY)
      message(STATUS "Found an installation of Cap'n Proto lite. Executable and library targets beyond libkj and libcapnp will be unavailable.")
    endif()
    # Lite mode doesn't include the executables, so return here.
    return()
  endif()
else()
  # If we didn't even find capnp or kj, then we didn't find anything usable.
  set(CapnProto_FOUND OFF)
  return()
endif()

# ========================================================================================
# Imported executable targets

get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)

# Add executable targets for the capnp compiler and plugins. This list must be kept manually in sync
# with the rest of the project.

add_executable(CapnProto::capnp_tool IMPORTED)
set_target_properties(CapnProto::capnp_tool PROPERTIES
  IMPORTED_LOCATION "${_IMPORT_PREFIX}/bin/capnp${CMAKE_EXECUTABLE_SUFFIX}"
)

add_executable(CapnProto::capnpc_cpp IMPORTED)
set_target_properties(CapnProto::capnpc_cpp PROPERTIES
  IMPORTED_LOCATION "${_IMPORT_PREFIX}/bin/capnpc-c++${CMAKE_EXECUTABLE_SUFFIX}"
)

add_executable(CapnProto::capnpc_capnp IMPORTED)
set_target_properties(CapnProto::capnpc_capnp PROPERTIES
  IMPORTED_LOCATION "${_IMPORT_PREFIX}/bin/capnpc-capnp${CMAKE_EXECUTABLE_SUFFIX}"
)
