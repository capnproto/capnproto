## Process this file with autoconf to produce configure.

# autoconf can't handle apostrophes in names...
AC_INIT([Capn Proto],[0.1.0],[capnproto@googlegroups.com],[capnproto])

AC_CONFIG_SRCDIR([src/capnproto/layout.c++])
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
AC_PROG_RANLIB
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([noext])
ACX_PTHREAD

LIBS="$PTHREAD_LIBS $LIBS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
