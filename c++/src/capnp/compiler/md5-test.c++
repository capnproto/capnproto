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

#include "md5.h"
#include <kj/compat/gtest.h>

namespace capnp {
namespace compiler {
namespace {

static kj::String doMd5(kj::StringPtr text) {
  Md5 md5;
  md5.update(text);
  return kj::str(md5.finishAsHex().cStr());
}

TEST(Md5, Sum) {
  EXPECT_STREQ("acbd18db4cc2f85cedef654fccc4a4d8", doMd5("foo").cStr());
  EXPECT_STREQ("37b51d194a7513e45b56f6524f2d51f2", doMd5("bar").cStr());
  EXPECT_STREQ("3858f62230ac3c915f300c664312c63f", doMd5("foobar").cStr());

  {
    Md5 md5;
    md5.update("foo");
    md5.update("bar");
    EXPECT_STREQ("3858f62230ac3c915f300c664312c63f", md5.finishAsHex().cStr());
  }

  EXPECT_STREQ("ebf2442d167a30ca4453f99abd8cddf4", doMd5(
      "Hello, this is a long string that is more than 64 bytes because the md5 code uses a "
      "buffer of 64 bytes.").cStr());

  {
    Md5 md5;
    md5.update("Hello, this is a long string ");
    md5.update("that is more than 64 bytes ");
    md5.update("because the md5 code uses a ");
    md5.update("buffer of 64 bytes.");
    EXPECT_STREQ("ebf2442d167a30ca4453f99abd8cddf4", md5.finishAsHex().cStr());
  }
}

}  // namespace
}  // namespace compiler
}  // namespace capnp
