## Process this file with autoconf to produce configure.

AC_INIT([Capn Proto],[1.1.0],[capnproto@googlegroups.com],[capnproto-c++])

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

AC_ARG_WITH([zlib],
  [AS_HELP_STRING([--with-zlib],
    [build libkj-gzip by linking against zlib @<:@default=check@:>@])],
  [],[with_zlib=check])

AC_ARG_WITH([openssl],
  [AS_HELP_STRING([--with-openssl],
    [build libkj-tls by linking against openssl @<:@default=check@:>@])],
  [],[with_openssl=check])

AC_ARG_WITH([fibers],
  [AS_HELP_STRING([--with-fibers],
    [build libkj-async with fibers @<:@default=check@:>@])],
  [],[with_fibers=check])

AC_ARG_ENABLE([reflection], [
  AS_HELP_STRING([--disable-reflection], [
    compile Cap'n Proto in "lite mode", in which all reflection APIs (schema.h, dynamic.h, etc.)
    are not included. Produces a smaller library at the cost of features. All programs built
    against the library MUST be compiled with -DCAPNP_LITE=1. Note that because the compiler
    itself uses reflection in its implementation, you must also use --with-external-capnp when
    using this option.])
  ], [
    case "${enableval}" in
      yes)
        lite_mode=no
        ;;
      no)
        lite_mode=yes
        AS_IF([test "$external_capnp" != "yes"], [
          AC_MSG_ERROR([you must specify --with-external-capnp when using --disable-reflection])
        ])
        ;;
      *)
        AC_MSG_ERROR([bad value ${enableval} for --enable-reflection])
        ;;
    esac
  ], [lite_mode=no])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_14

AS_CASE("${host_os}", *mingw*, [
    # We don't use pthreads on MinGW.
    PTHREAD_CFLAGS="-mthreads"
    PTHREAD_LIBS=""
    PTHREAD_CC=""
    ASYNC_LIBS="-lws2_32"
    AC_SUBST(PTHREAD_LIBS)
    AC_SUBST(PTHREAD_CFLAGS)
    AC_SUBST(PTHREAD_CC)
    AC_SUBST(ASYNC_LIBS)
  ], *, [
    ACX_PTHREAD
    ASYNC_LIBS=""
    AC_SUBST(ASYNC_LIBS)
  ])

LT_INIT

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

AM_CONDITIONAL([LITE_MODE], [test "$lite_mode" = "yes"])

AS_IF([test "$lite_mode" = "yes"], [
  CXXFLAGS="-DCAPNP_LITE $CXXFLAGS"
  CAPNP_LITE_FLAG=-DCAPNP_LITE
])
AC_SUBST([CAPNP_LITE_FLAG])

AC_SEARCH_LIBS(sched_yield, rt)

# Users will need to use the same -stdlib as us so we'd better let pkg-config know about it.
STDLIB_FLAG=`echo "$CXX $CXXFLAGS" | grep -o ' [[-]]stdlib=[[^ ]]*'`
AC_SUBST([STDLIB_FLAG])

LIBS="$PTHREAD_LIBS $LIBS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"

AC_DEFUN([CAPNP_PKG_CONFIG_FILES], [ \
  pkgconfig/capnp.pc \
  pkgconfig/capnpc.pc \
  pkgconfig/capnp-rpc.pc \
  pkgconfig/capnp-json.pc \
  pkgconfig/capnp-websocket.pc \
  pkgconfig/kj.pc \
  pkgconfig/kj-async.pc \
  pkgconfig/kj-http.pc \
  pkgconfig/kj-gzip.pc \
  pkgconfig/kj-tls.pc \
  pkgconfig/kj-test.pc \
])
AC_DEFUN([CAPNP_CMAKE_CONFIG_FILES], [ \
  cmake/CapnProtoConfig.cmake \
  cmake/CapnProtoConfigVersion.cmake \
])

[CAPNP_PKG_CONFIG_FILES]="CAPNP_PKG_CONFIG_FILES"
[CAPNP_CMAKE_CONFIG_FILES]="CAPNP_CMAKE_CONFIG_FILES"
AC_SUBST([CAPNP_PKG_CONFIG_FILES])
AC_SUBST([CAPNP_CMAKE_CONFIG_FILES])

# Don't include security release in soname -- we want to replace old binaries
# in this case.
SO_VERSION=$(echo $VERSION | sed -e 's/^\([0-9]*[.][0-9]*[.][0-9]*\)\([.][0-9]*\)*\(-.*\)*$/\1\3/g')
AC_SUBST([SO_VERSION])

# CapnProtoConfig.cmake.in needs these PACKAGE_* output variables.
PACKAGE_INIT="set([CAPNP_PKG_CONFIG_FILES] CAPNP_PKG_CONFIG_FILES)"
PACKAGE_CMAKE_INSTALL_FULL_INCLUDEDIR="\${CMAKE_CURRENT_LIST_DIR}/../../../include"
AC_SUBST([PACKAGE_INIT])
AC_SUBST([PACKAGE_CMAKE_INSTALL_FULL_INCLUDEDIR])

# CapnProtoConfigVersion.cmake.in needs PACKAGE_VERSION (already defined by AC_INIT) and
# CMAKE_SIZEOF_VOID_P output variables.
AC_CHECK_SIZEOF([void *])
AC_SUBST(CMAKE_SIZEOF_VOID_P, $ac_cv_sizeof_void_p)

# Detect presence of zlib, if it was not specified explicitly.
AS_IF([test "$with_zlib" = check], [
  AC_CHECK_LIB(z, deflate, [:], [
    with_zlib=no
  ])
  AC_CHECK_HEADER([zlib.h], [:], [
    with_zlib=no
  ])
  AS_IF([test "$with_zlib" = no], [
    AC_MSG_WARN("could not find zlib -- won't build libkj-gzip")
  ], [
    with_zlib=yes
  ])
])
AS_IF([test "$with_zlib" != no], [
  CXXFLAGS="$CXXFLAGS -DKJ_HAS_ZLIB"
])
AM_CONDITIONAL([BUILD_KJ_GZIP], [test "$with_zlib" != no])

# Detect presence of OpenSSL, if it was not specified explicitly.
AS_IF([test "$with_openssl" = check], [
  AC_CHECK_LIB(crypto, CRYPTO_new_ex_data, [:], [
    with_openssl=no
  ])
  AC_CHECK_LIB(ssl, OPENSSL_init_ssl, [:], [
    with_openssl=no
  ], [-lcrypto])
  AC_CHECK_HEADER([openssl/ssl.h], [:], [
    with_openssl=no
  ])
  AS_IF([test "$with_openssl" = no], [
    AC_MSG_WARN("could not find OpenSSL -- won't build libkj-tls")
  ], [
    with_openssl=yes
  ])
])
AS_IF([test "$with_openssl" != no], [
  CXXFLAGS="$CXXFLAGS -DKJ_HAS_OPENSSL"
])
AM_CONDITIONAL([BUILD_KJ_TLS], [test "$with_openssl" != no])

# Fibers don't work if exceptions are disabled, so default off in that case.
AS_IF([test "$with_fibers" != no], [
  AC_MSG_CHECKING([if exceptions are enabled])
  AC_COMPILE_IFELSE([void foo() { throw 1; }], [
    AC_MSG_RESULT([yes])
  ], [
    AS_IF([test "$with_fibers" = check], [
      AC_MSG_RESULT([no -- therefore, disabling fibers])
      with_fibers=no
    ], [
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([Fibers require exceptions, but your compiler flags disable exceptions. Please either enable exceptions or disable fibers (--without-fibers).])
    ])
  ])
])

# Check for library support necessary for fibers.
AS_IF([test "$with_fibers" != no], [
  case "${host_os}" in
    cygwin* | mingw* )
      # Fibers always work on Windows, where there's an explicit API for them.
      with_fibers=yes
      ;;
    * )
      # Fibers need the symbols getcontext, setcontext, swapcontext and makecontext.
      # We assume that makecontext implies the rest.
      libc_supports_fibers=yes
      AC_SEARCH_LIBS([makecontext], [], [], [
        libc_supports_fibers=no
      ])

      AS_IF([test "$libc_supports_fibers" = yes], [
          with_fibers=yes
        ], [
        # If getcontext does not exist in libc, try with libucontext
        ucontext_supports_fibers=yes
        AC_CHECK_LIB(ucontext, [makecontext], [], [
          ucontext_supports_fibers=no
        ])
        AS_IF([test "$ucontext_supports_fibers" = yes], [
          ASYNC_LIBS="$ASYNC_LIBS -lucontext"
          with_fibers=yes
        ], [
          AS_IF([test "$with_fibers" = yes], [
            AC_MSG_ERROR([Missing symbols required for fibers (makecontext, setcontext, ...). Disable fibers (--without-fibers) or install libucontext])
          ], [
            AC_MSG_WARN([could not find required symbols (makecontext, setcontext, ...) -- won't build with fibers])
            with_fibers=no
          ])
        ])
      ])
      ;;
  esac
])
AS_IF([test "$with_fibers" = yes], [
  CXXFLAGS="$CXXFLAGS -DKJ_USE_FIBERS"
], [
  CXXFLAGS="$CXXFLAGS -DKJ_USE_FIBERS=0"
])

# CapnProtoConfig.cmake.in needs these variables,
# we force them to NO because we don't need the CMake dependency for them,
# the dependencies are provided by the .pc files.
AC_SUBST(WITH_OPENSSL, NO)
AC_SUBST(_WITH_LIBUCONTEXT, NO)

AM_CONDITIONAL([HAS_FUZZING_ENGINE], [test "x$LIB_FUZZING_ENGINE" != "x"])

AC_CONFIG_FILES([Makefile] CAPNP_PKG_CONFIG_FILES CAPNP_CMAKE_CONFIG_FILES)
AC_OUTPUT
