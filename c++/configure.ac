## Process this file with autoconf to produce configure.

AC_INIT([Capn Proto],[0.2-dev],[capnproto@googlegroups.com],[capnproto-c++])

AC_CONFIG_SRCDIR([src/capnp/layout.c++])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# autoconf's default CXXFLAGS are usually "-g -O2".  A far more reasonable
# default is -O2 -NDEBUG.
AS_IF([test "x${ac_cv_env_CFLAGS_set}" = "x"],
      [CFLAGS="-O2 -DNDEBUG"])
AS_IF([test "x${ac_cv_env_CXXFLAGS_set}" = "x"],
      [CXXFLAGS="-O2 -DNDEBUG"])

AS_IF([test "x$CAPNPC" = "x"], [CAPNPC="capnpc"])
AC_SUBST([CAPNPC])

AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([noext])
ACX_PTHREAD
AC_PROG_LIBTOOL

LIBS="$PTHREAD_LIBS $LIBS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"

# AX_CXX_COMPILE_STDCXX_11 may have modified CXX, and we need gtest to use these modifications as
# well (especially -stdlib).  Note that this may cause ac_configure_args to contain CXX= twice, but
# the latter one should override the former.
ac_configure_args="$ac_configure_args 'CXX=$CXX'"
AC_CONFIG_SUBDIRS([gtest])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
