// Copyright (c) 2014, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "io.h"
#include "debug.h"
#include <gtest/gtest.h>
#include <unistd.h>

namespace kj {
namespace {

TEST(Io, WriteVec) {
  // Check that writing an array of arrays works even when some of the arrays are empty.  (This
  // used to not work in some cases.)

  int fds[2];
  KJ_SYSCALL(pipe(fds));

  FdInputStream in((AutoCloseFd(fds[0])));
  FdOutputStream out((AutoCloseFd(fds[1])));

  ArrayPtr<const byte> pieces[5] = {
    arrayPtr(implicitCast<const byte*>(nullptr), 0),
    arrayPtr(reinterpret_cast<const byte*>("foo"), 3),
    arrayPtr(implicitCast<const byte*>(nullptr), 0),
    arrayPtr(reinterpret_cast<const byte*>("bar"), 3),
    arrayPtr(implicitCast<const byte*>(nullptr), 0)
  };

  out.write(pieces);

  char buf[7];
  in.read(buf, 6);
  buf[6] = '\0';

  EXPECT_STREQ("foobar", buf);
}

}  // namespace
}  // namespace kj
