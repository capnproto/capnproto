#pragma once

// libc compatibility layer for target platforms that do not fully implement
// expected APIs, like older versions of Android

// -- Flags to inidicate that a compat layer for a particular function is required. --
#define KJ_COMPAT_ATFILE_EMULATE_READLINKAT 0
#define KJ_COMPAT_ATFILE_EMULATE_MKNODAT 0
#define KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT 0
#define KJ_COMPAT_ATFILE_EMULATE_LINKAT 0

// -- Compatibility layer requirement checks. A single positive test is sufficient to set a flag above --

// Glibc feature test macros checks
#ifdef __GLIBC__

#if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ >= 10)  // Condition: glibc >= 2.10
#if (!(_XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L))
  #undef KJ_COMPAT_ATFILE_EMULATE_READLINKAT
  #define KJ_COMPAT_ATFILE_EMULATE_READLINKAT 1
  #undef KJ_COMPAT_ATFILE_EMULATE_LINKAT
  #define KJ_COMPAT_ATFILE_EMULATE_LINKAT 1
  #undef KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT
  #define KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT 1
#endif
#else  // Condition: glibc < 2.10
#ifndef _ATFILE_SOURCE
  #undef KJ_COMPAT_ATFILE_EMULATE_READLINKAT
  #define KJ_COMPAT_ATFILE_EMULATE_READLINKAT 1
  #undef KJ_COMPAT_ATFILE_EMULATE_LINKAT
  #define KJ_COMPAT_ATFILE_EMULATE_LINKAT 1
  #undef KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT
  #define KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT 1
#endif
#endif

#if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ >= 19)  // Condition: glibc >= 2.19
#if (!(defined(_DEFAULT_SOURCE) || (_XOPEN_SOURCE >= 500)))
  #undefine KJ_COMPAT_ATFILE_EMULATE_MKNODAT
  #define KJ_COMPAT_ATFILE_EMULATE_MKNODAT 1
#endif
#else // Condition: glibc < 2.19
#if (!(defined(_BSD_SOURCE) || defined(_SVID_SOURCE))
  #undefine KJ_COMPAT_ATFILE_EMULATE_MKNODAT
  #define KJ_COMPAT_ATFILE_EMULATE_MKNODAT 1
#endif
#endif

#endif // __GLIBC__

// Android API version checks
#ifdef __ANDROID__
#if (__ANDROID_API < 21) // Condition: Android API < 21
#undef KJ_COMPAT_ATFILE_EMULATE_READLINKAT
#define KJ_COMPAT_ATFILE_EMULATE_READLINKAT 1
#undef KJ_COMPAT_ATFILE_EMULATE_LINKAT
#define KJ_COMPAT_ATFILE_EMULATE_LINKAT 1
#undef KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT
#define KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT 1
#undef KJ_COMPAT_ATFILE_EMULATE_MKNODAT
#define KJ_COMPAT_ATFILE_EMULATE_MKNODAT 1
#endif
#endif // __ANDROID__

// -- Includes, depending on emulation's dependencies
#if (KJ_COMPAT_ATFILE_EMULATE_READLINKAT == 1)
#include <stddef.h>   // size_t
#endif
#if KJ_COMPAT_ATFILE_EMULATE_MKNODAT == 1
#include <sys/stat.h>  // mode_t, dev_t
#endif

// -- Additional atfile constants, activated if one of emulations is required ---
#if (KJ_COMPAT_ATFILE_EMULATE_READLINKAT == 1) ||       \
  (KJ_COMPAT_ATFILE_EMULATE_MKNODAT == 1) ||            \
  (KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT == 1) ||          \
  (KJ_COMPAT_ATFILE_EMULATE_LINKAT == 1)
#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#endif


namespace kj {
  #if (KJ_COMPAT_ATFILE_EMULATE_READLINKAT == 1)
  int readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
  #endif
  #if (KJ_COMPAT_ATFILE_EMULATE_SYMLINKAT == 1)
  int symlinkat(const char *oldpath, int newdirfd, const char *newpath);
  #endif
  #if KJ_COMPAT_ATFILE_EMULATE_LINKAT == 1
  int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
  #endif
  #if KJ_COMPAT_ATFILE_EMULATE_MKNODAT == 1
  int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);
  #endif
}
