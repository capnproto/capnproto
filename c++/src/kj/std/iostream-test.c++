// Copyright (c) 2014 Sandstorm Development Group, Inc. and contributors
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

#include "iostream.h"
#include <sstream>
#include <kj/compat/gtest.h>

namespace kj {
namespace std {
namespace {

TEST(StdIoStream, WriteVec) {
  // Check that writing an array of arrays works even when some of the arrays
  // are empty.  (This used to not work in some cases.)

  ::std::stringstream ss;

  StdInputStream in(ss);
  StdOutputStream out(ss);

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

TEST(StdIoStream, TryReadToEndOfFile) {
  // Check that tryRead works when eof is reached before minBytes.

  ::std::stringstream ss;

  StdInputStream in(ss);
  StdOutputStream out(ss);

  const void* bytes = "foobar";

  out.write(bytes, 6);

  char buf[9];
  in.tryRead(buf, 8, 8);
  buf[6] = '\0';

  EXPECT_STREQ("foobar", buf);
}

TEST(StdIoStream, ReadToEndOfFile) {
  // Check that read throws an exception when eof is reached before specified
  // bytes.

  ::std::stringstream ss;

  StdInputStream in(ss);
  StdOutputStream out(ss);

  const void* bytes = "foobar";

  out.write(bytes, 6);

  char buf[9];

  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    in.read(buf, 8, 8);
  });
  buf[6] = '\0';

  ASSERT_FALSE(e == nullptr);

  // Ensure that the value is still read up to the EOF.
  EXPECT_STREQ("foobar", buf);
}

}  // namespace
}  // namespace std
}  // namespace kj
