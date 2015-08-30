#
# Macro that checks for C++11 support and sets compiler flags
# If C++11 is not present 
#
# Macro:
#   CheckCXX11(MESSAGE_SEVERITY)
#       MESSAGE_SEVERITY        it's not mandatory and has default value of "STATUS"
#                               this is severity of message if no C++11 was found
#
# Output variables:
#   COMPILER_SUPPORTS_CXX11     compiler supports `-std=c++11` flag
#   COMPILER_SUPPORTS_CXX0X     compiler supports `-std=c++0x` flag
#
# Copyright (c) 2015, Marek Pikuła <marek@pikula.co>
# All rights reserved.
#
# Distributed under the OSI-approved BSD License (the "License") see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the License for more information.
#

macro (CheckCXX11)
    include (CheckCXXCompilerFlag)
    check_cxx_compiler_flag ("-std=c++11" COMPILER_SUPPORTS_CXX11)
    check_cxx_compiler_flag ("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
    if (COMPILER_SUPPORTS_CXX11)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    elseif (COMPILER_SUPPORTS_CXX0X)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    else ()
        if (${ARGC})
            set (MESSAGE_SEVERITY ${ARGV0})
        else ()
            set (MESSAGE_SEVERITY "STATUS")
        endif ()
        message (${MESSAGE_SEVERITY}
                 "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
        unset (MESSAGE_SEVERITY)
    endif ()
endmacro ()
