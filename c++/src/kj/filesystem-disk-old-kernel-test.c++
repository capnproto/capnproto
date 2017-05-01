// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#if __linux__ && __x86_64__ && defined(__has_include)
#if __has_include(<linux/seccomp.h>) && \
    __has_include(<linux/filter.h>) && \
    __has_include(<linux/audit.h>) && \
    __has_include(<linux/signal.h>) && \
    __has_include(<sys/ptrace.h>)
// This test re-runs filesystem-disk-test.c++ with newfangled Linux kernel features disabled.
//
// This test must be compiled as a separate program, since it alters the calling process by
// enabling seccomp to disable the kernel features.

#include <syscall.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <kj/debug.h>

#ifdef SECCOMP_SET_MODE_FILTER

namespace {

#if 0
// Source code of the seccomp filter:

  ld [0]                  /* offsetof(struct seccomp_data, nr) */
  jeq #8, lseek           /* __NR_lseek */
  jeq #16, inval          /* __NR_ioctl */
  jeq #40, nosys          /* __NR_sendfile */
  jeq #257, openat        /* __NR_openat */
  jeq #285, notsup        /* __NR_fallocate */
  jeq #316, nosys         /* __NR_renameat2 */
  jmp good

openat:
  ld [32]                 /* offsetof(struct seccomp_data, args[2]), aka flags */
  and #4259840            /* O_TMPFILE */
  jeq #4259840, notsup
  jmp good

lseek:
  ld [32]                 /* offsetof(struct seccomp_data, args[2]), aka whence */
  jeq #3, inval           /* SEEK_DATA */
  jeq #4, inval           /* SEEK_HOLE */
  jmp good

inval:  ret #0x00050016  /* SECCOMP_RET_ERRNO | EINVAL */
nosys:  ret #0x00050026  /* SECCOMP_RET_ERRNO | ENOSYS */
notsup: ret #0x0005005f  /* SECCOMP_RET_ERRNO | EOPNOTSUPP */
good:   ret #0x7fff0000  /* SECCOMP_RET_ALLOW */

#endif

struct SetupSeccompForFilesystemTest {
  SetupSeccompForFilesystemTest() {
    struct sock_filter filter[] {
      { 0x20,  0,  0, 0000000000 },
      { 0x15, 10,  0, 0x00000008 },
      { 0x15, 13,  0, 0x00000010 },
      { 0x15, 13,  0, 0x00000028 },
      { 0x15,  3,  0, 0x00000101 },
      { 0x15, 12,  0, 0x0000011d },
      { 0x15, 10,  0, 0x0000013c },
      { 0x05,  0,  0, 0x0000000b },
      { 0x20,  0,  0, 0x00000020 },
      { 0x54,  0,  0, 0x00410000 },
      { 0x15,  7,  0, 0x00410000 },
      { 0x05,  0,  0, 0x00000007 },
      { 0x20,  0,  0, 0x00000020 },
      { 0x15,  2,  0, 0x00000003 },
      { 0x15,  1,  0, 0x00000004 },
      { 0x05,  0,  0, 0x00000003 },
      { 0x06,  0,  0, 0x00050016 },
      { 0x06,  0,  0, 0x00050026 },
      { 0x06,  0,  0, 0x0005005f },
      { 0x06,  0,  0, 0x7fff0000 },
    };

    struct sock_fprog prog { sizeof(filter) / sizeof(filter[0]), filter };

    KJ_SYSCALL(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
    KJ_SYSCALL(syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog));
  }
};

SetupSeccompForFilesystemTest setupSeccompForFilesystemTest;

}  // namespace

#define HOLES_NOT_SUPPORTED

// OK, now run all the regular filesystem tests!
#include "filesystem-disk-test.c++"

#endif
#endif
#endif
