# ============================================================================
#  http://www.gnu.org/software/autoconf-archive/ax_cxx_compile_stdcxx_11.html
#  Additionally modified to detect -stdlib by Kenton Varda.
#  Further modified for C++14 by Kenton Varda.
# ============================================================================
#
# SYNOPSIS
#
#   AX_CXX_COMPILE_STDCXX_14([ext|noext])
#
# DESCRIPTION
#
#   Check for baseline language coverage in the compiler for the C++14
#   standard; if necessary, add switches to CXXFLAGS to enable support.
#   Errors out if no mode that supports C++14 baseline syntax can be found.
#   The argument, if specified, indicates whether you insist on an extended
#   mode (e.g. -std=gnu++14) or a strict conformance mode (e.g. -std=c++14).
#   If neither is specified, you get whatever works, with preference for an
#   extended mode.
#
#   Additionally, check if the standard library supports C++11.  If not,
#   try adding -stdlib=libc++ to see if that fixes it.  This is needed e.g.
#   on Mac OSX 10.8, which ships with a very old libstdc++ but a relatively
#   new libc++.
#
#   Both flags are actually added to CXX rather than CXXFLAGS to work around
#   a bug in libtool: -stdlib is stripped from CXXFLAGS when linking dynamic
#   libraries because it is not recognized.  A patch was committed to mainline
#   libtool in February 2012 but as of June 2013 there has not yet been a
#   release containing this patch.
#      http://git.savannah.gnu.org/gitweb/?p=libtool.git;a=commit;h=c0c49f289f22ae670066657c60905986da3b555f
#
# LICENSE
#
#   Copyright (c) 2008 Benjamin Kosnik <bkoz@redhat.com>
#   Copyright (c) 2012 Zack Weinberg <zackw@panix.com>
#   Copyright (c) 2013 Kenton Varda <temporal@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

m4_define([_AX_CXX_COMPILE_STDCXX_14_testbody], [[
  template <typename T>
  struct check
  {
    static_assert(sizeof(int) <= sizeof(T), "not big enough");
  };

  typedef check<check<bool>> right_angle_brackets;

  int a;
  decltype(a) b;

  typedef check<int> check_type;
  check_type c;
  check_type&& cr = static_cast<check_type&&>(c);

  // GCC 4.7 introduced __float128 and makes reference to it in type_traits.
  // Clang doesn't implement it, so produces an error.  Using -std=c++11
  // instead of -std=gnu++11 works around the problem.  But on some
  // platforms we need -std=gnu++11.  So we want to make sure the test of
  // -std=gnu++11 fails only where this problem is present, and we hope that
  // -std=c++11 is always an acceptable fallback in these cases.  Complicating
  // matters, though, is that we don't want to fail here if the platform is
  // completely missing a C++11 standard library, because we want to probe that
  // in a later test.  It happens, though, that Clang allows us to check
  // whether a header exists at all before we include it.
  //
  // So, if we detect that __has_include is available (which it is on Clang),
  // and we use it to detect that <type_traits> (a C++11 header) exists, then
  // we go ahead and #include it to see if it breaks.  In all other cases, we
  // don't #include it at all.
  #ifdef __has_include
  #if __has_include(<type_traits>)
  #include <type_traits>
  #endif
  #endif

  // C++14 stuff
  auto deduceReturnType(int i) { return i; }

  auto genericLambda = [](auto x, auto y) { return x + y; };
  auto captureExpressions = [x = 123]() { return x; };

  // Avoid unused variable warnings.
  int foo() {
    return genericLambda(1, 2) + captureExpressions();
  }
]])

m4_define([_AX_CXX_COMPILE_STDCXX_11_testbody_lib], [
  #include <initializer_list>
  #include <unordered_map>
  #include <atomic>
  #include <thread>
])

AC_DEFUN([AX_CXX_COMPILE_STDCXX_14], [dnl
  m4_if([$1], [], [],
        [$1], [ext], [],
        [$1], [noext], [],
        [m4_fatal([invalid argument `$1' to AX_CXX_COMPILE_STDCXX_14])])dnl
  AC_LANG_ASSERT([C++])dnl
  ac_success=no
  AC_CACHE_CHECK(whether $CXX supports C++14 features by default,
  ax_cv_cxx_compile_cxx14,
  [AC_COMPILE_IFELSE([AC_LANG_SOURCE([_AX_CXX_COMPILE_STDCXX_14_testbody])],
    [ax_cv_cxx_compile_cxx14=yes],
    [ax_cv_cxx_compile_cxx14=no])])
  if test x$ax_cv_cxx_compile_cxx14 = xyes; then
    ac_success=yes
  fi

  m4_if([$1], [noext], [], [dnl
  if test x$ac_success = xno; then
    for switch in -std=gnu++14 -std=gnu++1y; do
      cachevar=AS_TR_SH([ax_cv_cxx_compile_cxx14_$switch])
      AC_CACHE_CHECK(whether $CXX supports C++14 features with $switch,
                     $cachevar,
        [ac_save_CXX="$CXX"
         CXX="$CXX $switch"
         AC_COMPILE_IFELSE([AC_LANG_SOURCE([_AX_CXX_COMPILE_STDCXX_14_testbody])],
          [eval $cachevar=yes],
          [eval $cachevar=no])
         CXX="$ac_save_CXX"])
      if eval test x\$$cachevar = xyes; then
        CXX="$CXX $switch"
        ac_success=yes
        break
      fi
    done
  fi])

  m4_if([$1], [ext], [], [dnl
  if test x$ac_success = xno; then
    for switch in -std=c++14 -std=c++1y; do
      cachevar=AS_TR_SH([ax_cv_cxx_compile_cxx14_$switch])
      AC_CACHE_CHECK(whether $CXX supports C++14 features with $switch,
                     $cachevar,
        [ac_save_CXX="$CXX"
         CXX="$CXX $switch"
         AC_COMPILE_IFELSE([AC_LANG_SOURCE([_AX_CXX_COMPILE_STDCXX_14_testbody])],
          [eval $cachevar=yes],
          [eval $cachevar=no])
         CXX="$ac_save_CXX"])
      if eval test x\$$cachevar = xyes; then
        CXX="$CXX $switch"
        ac_success=yes
        break
      fi
    done
  fi])

  if test x$ac_success = xno; then
    AC_MSG_ERROR([*** A compiler with support for C++14 language features is required.])
  else
    ac_success=no
    AC_CACHE_CHECK(whether $CXX supports C++11 library features by default,
                   ax_cv_cxx_compile_cxx11_lib,
      [AC_COMPILE_IFELSE([AC_LANG_SOURCE([_AX_CXX_COMPILE_STDCXX_11_testbody_lib])],
         [ax_cv_cxx_compile_cxx11_lib=yes],
         [ax_cv_cxx_compile_cxx11_lib=no])
      ])
    if test x$ax_cv_cxx_compile_cxx11_lib = xyes; then
      ac_success=yes
    else
      # Try with -stdlib=libc++
      AC_CACHE_CHECK(whether $CXX supports C++11 library features with -stdlib=libc++,
                     ax_cv_cxx_compile_cxx11_lib_libcxx,
        [ac_save_CXX="$CXX"
         CXX="$CXX -stdlib=libc++"
         AC_COMPILE_IFELSE([AC_LANG_SOURCE([_AX_CXX_COMPILE_STDCXX_11_testbody_lib])],
          [eval ax_cv_cxx_compile_cxx11_lib_libcxx=yes],
          [eval ax_cv_cxx_compile_cxx11_lib_libcxx=no])
         CXX="$ac_save_CXX"])
      if eval test x$ax_cv_cxx_compile_cxx11_lib_libcxx = xyes; then
        CXX="$CXX -stdlib=libc++"
        ac_success=yes
        break
      fi
    fi

    if test x$ac_success = xno; then
      AC_MSG_ERROR([*** A C++ library with support for C++11 features is required.])
    fi
  fi
])
