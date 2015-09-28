#
# Macro that checks for C++11 support and sets compiler flags
# If C++11 is not present it throws message with MESSAGE_SEVERITY.
# Bear in mind, that for MSVC it's just passing "-std=c++0x" to compiler.
#
# Macro:
#   CheckCXX11(MESSAGE_SEVERITY)
#       MESSAGE_SEVERITY        it's not mandatory and has default value of "STATUS"
#                               this is severity of message if no C++11 was found
#
# Output variables:
#   COMPILER_SUPPORTS_CXX11     ON when compiler supports C++11
#
# Copyright (c) 2015, Marek Pikuła <marek@pikula.co>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
# TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
# CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

macro (CheckCXX11)
    set (COMPILER_SUPPORTS_CXX11 OFF)
    
    if (MSVC)
        # MSVC has C++11 turned on by default
        set (COMPILER_SUPPORTS_CXX11 ON)
    else ()
        include (CheckCXXCompilerFlag)
        if (CMAKE_COMPILER_IS_GNUCXX)
            check_cxx_compiler_flag ("-std=gnu++11" GNUXX11)
        endif ()
        if (NOT GNUXX11)
            check_cxx_compiler_flag ("-std=c++11" CXX11)
            check_cxx_compiler_flag ("-std=c++0x" CXX0X)
        endif ()
        
        if (GNUXX11)
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
            set (COMPILER_SUPPORTS_CXX11 ON)
        elseif (CXX11)
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
            set (COMPILER_SUPPORTS_CXX11 ON)
        elseif (CXX0X)
            set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
            set (COMPILER_SUPPORTS_CXX11 ON)
        endif ()
    endif ()
    
    if (NOT COMPILER_SUPPORTS_CXX11)
        if (${ARGC})
            set (MESSAGE_SEVERITY ${ARGV0})
        else ()
            set (MESSAGE_SEVERITY "STATUS")
        endif ()
        message (${MESSAGE_SEVERITY} "C++11 support in ${CMAKE_CXX_COMPILER} compiler was not detected. Please use a different C++ compiler.")
        unset (MESSAGE_SEVERITY)
    endif ()
    
    unset (GNUXX11)
    unset (CXX11)
    unset (CXX0X)
endmacro ()
