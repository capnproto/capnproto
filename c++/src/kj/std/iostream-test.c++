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

  ArrayPtr<const byte> pieces[5] = { nullptr, "foo"_kjb, nullptr, "bar"_kjb, nullptr, };

  out.write(pieces);

  byte buf[6]{};
  in.read(buf);

  EXPECT_EQ("foobar"_kjb, buf);
}

TEST(StdIoStream, TryReadToEndOfFile) {
  // Check that tryRead works when eof is reached before minBytes.

  ::std::stringstream ss;

  StdInputStream in(ss);
  StdOutputStream out(ss);

  out.write("foobar"_kjb);

  byte buf[9]{};
  auto amount = in.tryRead(buf, 8);
  EXPECT_EQ("foobar"_kjb, arrayPtr(buf).first(amount));
}

TEST(StdIoStream, ReadToEndOfFile) {
  // Check that read throws an exception when eof is reached before specified
  // bytes.

  ::std::stringstream ss;

  StdInputStream in(ss);
  StdOutputStream out(ss);

  out.write("foobar"_kjb);

  byte buf[8]{};

  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    in.read(buf);
  });

  ASSERT_FALSE(e == kj::none);

  // Ensure that the value is still read up to the EOF.
  EXPECT_EQ("foobar"_kjb, arrayPtr(buf).first(6));
}

}  // namespace
}  // namespace std
}  // namespace kj
