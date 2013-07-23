// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
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

#include "md5.h"
#include <gtest/gtest.h>

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
