project("Cap'n Proto" CXX)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckIncludeFileCXX)
check_include_file_cxx(initializer_list HAS_CXX11 "-std=c++11")
if(NOT HAS_CXX11)
  message(SEND_ERROR "Requires a C++11 compiler and standard library.")
endif()

# Set installation paths; these can be overridden in the cache.
set(EXEC_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}"
  CACHE PATH "Base installation path for executables."
)
set(BIN_INSTALL_DIR "${EXEC_INSTALL_PREFIX}/bin"
  CACHE PATH "Installation directory for binaries (default: prefix/bin)."
)
set(LIB_INSTALL_DIR "${EXEC_INSTALL_PREFIX}/lib"
  CACHE PATH "Installation directory for libraries (default: prefix/lib)."
)
set(INCLUDE_INSTALL_DIR "${EXEC_INSTALL_PREFIX}/include"
  CACHE PATH "Installation directory for header files (default: prefix/include)."
)

add_subdirectory(c++)
