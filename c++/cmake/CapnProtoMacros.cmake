# CAPNP_GENERATE_CPP ===========================================================
#
# Example usage:
#   find_package(CapnProto)
#   capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS schema.capnp)
#   include_directories(${CMAKE_CURRENT_BINARY_DIR})
#   add_executable(foo main.cpp ${CAPNP_SRCS})
#   target_link_libraries(foo CapnProto::capnp-rpc)
#
#  If you are using not using the RPC features you can use
#  'CapnProto::capnp' in target_link_libraries call
#
# Configuration variables (optional):
#   CAPNPC_OUTPUT_DIR
#       Directory to place compiled schema sources (default: CMAKE_CURRENT_BINARY_DIR).
#   CAPNPC_IMPORT_DIRS
#       List of additional include directories for the schema compiler.
#       (CMAKE_CURRENT_SOURCE_DIR and CAPNP_INCLUDE_DIRECTORY are always included.)
#   CAPNPC_SRC_PREFIX
#       Schema file source prefix (default: CMAKE_CURRENT_SOURCE_DIR).
#   CAPNPC_FLAGS
#       Additional flags to pass to the schema compiler.
#
# TODO: convert to cmake_parse_arguments

function(CAPNP_GENERATE_CPP SOURCES HEADERS)
  if(NOT ARGN)
    message(SEND_ERROR "CAPNP_GENERATE_CPP() called without any source files.")
  endif()
  set(tool_depends ${EMPTY_STRING})
  #Use cmake targets available
  if(TARGET capnp_tool)
    set(CAPNP_EXECUTABLE capnp_tool)
    GET_TARGET_PROPERTY(CAPNPC_CXX_EXECUTABLE capnpc_cpp CAPNPC_CXX_EXECUTABLE)
    GET_TARGET_PROPERTY(CAPNP_INCLUDE_DIRECTORY capnp_tool CAPNP_INCLUDE_DIRECTORY)
    list(APPEND tool_depends capnp_tool capnpc_cpp)
  endif()
  if(NOT CAPNP_EXECUTABLE)
    message(SEND_ERROR "Could not locate capnp executable (CAPNP_EXECUTABLE).")
  endif()
  if(NOT CAPNPC_CXX_EXECUTABLE)
    message(SEND_ERROR "Could not locate capnpc-c++ executable (CAPNPC_CXX_EXECUTABLE).")
  endif()
  if(NOT CAPNP_INCLUDE_DIRECTORY)
    message(SEND_ERROR "Could not locate capnp header files (CAPNP_INCLUDE_DIRECTORY).")
  endif()

  # Default compiler includes
  set(include_path -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${CAPNP_INCLUDE_DIRECTORY})

  if(DEFINED CAPNPC_IMPORT_DIRS)
    # Append each directory as a series of '-I' flags in ${include_path}
    foreach(directory ${CAPNPC_IMPORT_DIRS})
      get_filename_component(absolute_path "${directory}" ABSOLUTE)
      list(APPEND include_path -I ${absolute_path})
    endforeach()
  endif()

  if(DEFINED CAPNPC_OUTPUT_DIR)
    # Prepend a ':' to get the format for the '-o' flag right
    set(output_dir ":${CAPNPC_OUTPUT_DIR}")
  else()
    set(output_dir ":.")
  endif()

  if(NOT DEFINED CAPNPC_SRC_PREFIX)
    set(CAPNPC_SRC_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  get_filename_component(CAPNPC_SRC_PREFIX "${CAPNPC_SRC_PREFIX}" ABSOLUTE)

  set(${SOURCES})
  set(${HEADERS})
  foreach(schema_file ${ARGN})
    if(NOT EXISTS "${CAPNPC_SRC_PREFIX}/${schema_file}")
      message(FATAL_ERROR "Cap'n Proto schema file '${CAPNPC_SRC_PREFIX}/${schema_file}' does not exist!")
    endif()
    get_filename_component(file_path "${schema_file}" ABSOLUTE)
    get_filename_component(file_dir "${file_path}" PATH)

    # Figure out where the output files will go
    if (NOT DEFINED CAPNPC_OUTPUT_DIR)
      set(CAPNPC_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/")
    endif()
    # Output files are placed in CAPNPC_OUTPUT_DIR, at a location as if they were
    # relative to CAPNPC_SRC_PREFIX.
    string(LENGTH "${CAPNPC_SRC_PREFIX}" prefix_len)
    string(SUBSTRING "${file_path}" 0 ${prefix_len} output_prefix)
    if(NOT "${CAPNPC_SRC_PREFIX}" STREQUAL "${output_prefix}")
      message(SEND_ERROR "Could not determine output path for '${schema_file}' ('${file_path}') with source prefix '${CAPNPC_SRC_PREFIX}' into '${CAPNPC_OUTPUT_DIR}'.")
    endif()

    string(SUBSTRING "${file_path}" ${prefix_len} -1 output_path)
    set(output_base "${CAPNPC_OUTPUT_DIR}${output_path}")

    add_custom_command(
      OUTPUT "${output_base}.c++" "${output_base}.h"
      COMMAND "${CAPNP_EXECUTABLE}"
      ARGS compile
          -o ${CAPNPC_CXX_EXECUTABLE}${output_dir}
          --src-prefix ${CAPNPC_SRC_PREFIX}
          ${include_path}
          ${CAPNPC_FLAGS}
          ${file_path}
      DEPENDS "${schema_file}" ${tool_depends}
      COMMENT "Compiling Cap'n Proto schema ${schema_file}"
      VERBATIM
    )

    list(APPEND ${SOURCES} "${output_base}.c++")
    list(APPEND ${HEADERS} "${output_base}.h")
  endforeach()

  set_source_files_properties(${${SOURCES}} ${${HEADERS}} PROPERTIES GENERATED TRUE)
  set(${SOURCES} ${${SOURCES}} PARENT_SCOPE)
  set(${HEADERS} ${${HEADERS}} PARENT_SCOPE)
endfunction()
