## Process this file with autoconf to produce configure.

AC_INIT([Capn Proto],[0.5-dev],[capnproto@googlegroups.com],[capnproto-c++])

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

AM_INIT_AUTOMAKE([tar-ustar])

AC_ARG_WITH([external-capnp],
  [AS_HELP_STRING([--with-external-capnp],
    [use the system capnp binary (or the one specified with $CAPNP) instead of compiling a new
     one (useful for cross-compiling)])],
  [external_capnp=yes],[external_capnp=no])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11
ACX_PTHREAD
AC_PROG_LIBTOOL

AS_IF([test "$external_capnp" != "no"], [
	AS_IF([test "x$CAPNP" = "x"], [CAPNP="capnp"], [with_capnp=yes])
	AS_IF([test "x$CAPNPC_CXX" = "x"], [
	  # CAPNPC_CXX was not specified.  Choose a reasonable default.
	  AS_CASE([$CAPNP], [*/*], [
	    # $CAPNP contains a slash, so it's not on $PATH.  Assume capnpc-c++ is not either, but is
	    # in the same directory.
	    CAPNPC_CXX=`dirname $CAPNP`/capnpc-c++
	  ], [
	    # $CAPNP is on $PATH, so tell it to find the plugin on $PATH as well.
	    CAPNPC_CXX="c++"
	  ])
	])
	AC_SUBST([CAPNP])
	AC_SUBST([CAPNPC_CXX])
])
AM_CONDITIONAL([USE_EXTERNAL_CAPNP], [test "$external_capnp" != "no"])

AC_SEARCH_LIBS(sched_yield, rt)

# Users will need to use the same -stdlib as us so we'd better let pkg-config know about it.
STDLIB_FLAG=`echo "$CXX $CXXFLAGS" | grep -o ' [[-]]stdlib=[[^ ]]*'`
AC_SUBST([STDLIB_FLAG])

LIBS="$PTHREAD_LIBS $LIBS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"

# AX_CXX_COMPILE_STDCXX_11 may have modified CXX, and we need gtest to use these modifications as
# well (especially -stdlib).  Note that this may cause ac_configure_args to contain CXX= twice, but
# the latter one should override the former.
ac_configure_args="$ac_configure_args 'CXX=$CXX'"
AC_CONFIG_SUBDIRS([gtest])

AC_CONFIG_FILES([Makefile capnp.pc capnp-rpc.pc])
AC_OUTPUT
