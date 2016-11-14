// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "io.h"
#include "debug.h"
#include "miniposix.h"
#include <kj/compat/gtest.h>

namespace kj {
namespace {

TEST(Io, WriteVec) {
  // Check that writing an array of arrays works even when some of the arrays are empty.  (This
  // used to not work in some cases.)

  int fds[2];
  KJ_SYSCALL(miniposix::pipe(fds));

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

KJ_TEST("stringify AutoCloseFd") {
  int fds[2];
  KJ_SYSCALL(miniposix::pipe(fds));
  AutoCloseFd in(fds[0]), out(fds[1]);

  KJ_EXPECT(kj::str(in) == kj::str(fds[0]), in, fds[0]);
}

KJ_TEST("VectorOutputStream") {
  VectorOutputStream output(16);
  auto buf = output.getWriteBuffer();
  KJ_ASSERT(buf.size() == 16);

  for (auto i: kj::indices(buf)) {
    buf[i] = 'a' + i;
  }

  output.write(buf.begin(), 4);
  KJ_ASSERT(output.getArray().begin() == buf.begin());
  KJ_ASSERT(output.getArray().size() == 4);

  auto buf2 = output.getWriteBuffer();
  KJ_ASSERT(buf2.end() == buf.end());
  KJ_ASSERT(buf2.size() == 12);

  output.write(buf2.begin(), buf2.size());
  KJ_ASSERT(output.getArray().begin() == buf.begin());
  KJ_ASSERT(output.getArray().size() == 16);

  auto buf3 = output.getWriteBuffer();
  KJ_ASSERT(buf3.size() == 16);
  KJ_ASSERT(output.getArray().begin() != buf.begin());
  KJ_ASSERT(output.getArray().end() == buf3.begin());
  KJ_ASSERT(kj::str(output.getArray().asChars()) == "abcdefghijklmnop");

  byte junk[24];
  for (auto i: kj::indices(junk)) {
    junk[i] = 'A' + i;
  }

  output.write(junk, 4);
  KJ_ASSERT(output.getArray().begin() != buf.begin());
  KJ_ASSERT(output.getArray().end() == buf3.begin() + 4);
  KJ_ASSERT(kj::str(output.getArray().asChars()) == "abcdefghijklmnopABCD");

  output.write(junk + 4, 20);
  KJ_ASSERT(output.getArray().begin() != buf.begin());
  KJ_ASSERT(output.getArray().end() != buf3.begin() + 24);
  KJ_ASSERT(kj::str(output.getArray().asChars()) == "abcdefghijklmnopABCDEFGHIJKLMNOPQRSTUVWX");

  KJ_ASSERT(output.getWriteBuffer().size() == 24);
  KJ_ASSERT(output.getWriteBuffer().begin() == output.getArray().begin() + 40);
}

}  // namespace
}  // namespace kj
