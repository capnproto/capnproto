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

#define CAPNPROTO_PRIVATE
#include "stringify.h"
#include "message.h"
#include "logging.h"
#include <gtest/gtest.h>
#include "test-util.h"

namespace capnproto {
namespace internal {
namespace {

TEST(Stringify, DebugString) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  EXPECT_STREQ("()", root.debugString().cStr());

  initTestMessage(root);

  EXPECT_STREQ("("
      "boolField = true, "
      "int8Field = -123, "
      "int16Field = -12345, "
      "int32Field = -12345678, "
      "int64Field = -123456789012345, "
      "uInt8Field = 234, "
      "uInt16Field = 45678, "
      "uInt32Field = 3456789012, "
      "uInt64Field = 12345678901234567890, "
      "float32Field = 1234.5, "
      "float64Field = -1.23e47, "
      "textField = \"foo\", "
      "dataField = \"bar\", "
      "structField = ("
          "boolField = true, "
          "int8Field = -12, "
          "int16Field = 3456, "
          "int32Field = -78901234, "
          "int64Field = 56789012345678, "
          "uInt8Field = 90, "
          "uInt16Field = 1234, "
          "uInt32Field = 56789012, "
          "uInt64Field = 345678901234567890, "
          "float32Field = -1.25e-10, "
          "float64Field = 345, "
          "textField = \"baz\", "
          "dataField = \"qux\", "
          "structField = ("
              "textField = \"nested\", "
              "structField = (textField = \"really nested\")), "
          "enumField = baz, "
          "voidList = [void, void, void], "
          "boolList = [false, true, false, true, true], "
          "int8List = [12, -34, -128, 127], "
          "int16List = [1234, -5678, -32768, 32767], "
          "int32List = [12345678, -90123456, -2147483648, 2147483647], "
          "int64List = [123456789012345, -678901234567890, "
                       "-9223372036854775808, 9223372036854775807], "
          "uInt8List = [12, 34, 0, 255], "
          "uInt16List = [1234, 5678, 0, 65535], "
          "uInt32List = [12345678, 90123456, 0, 4294967295], "
          "uInt64List = [123456789012345, 678901234567890, 0, 18446744073709551615], "
          "float32List = [0, 1234567, 1e37, -1e37, 1e-37, -1e-37], "
          "float64List = [0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306], "
          "textList = [\"quux\", \"corge\", \"grault\"], "
          "dataList = [\"garply\", \"waldo\", \"fred\"], "
          "structList = ["
              "(textField = \"x structlist 1\"), "
              "(textField = \"x structlist 2\"), "
              "(textField = \"x structlist 3\")], "
          "enumList = [qux, bar, grault]), "
      "enumField = corge, "
      "voidList = [void, void, void, void, void, void], "
      "boolList = [true, false, false, true], "
      "int8List = [111, -111], "
      "int16List = [11111, -11111], "
      "int32List = [111111111, -111111111], "
      "int64List = [1111111111111111111, -1111111111111111111], "
      "uInt8List = [111, 222], "
      "uInt16List = [33333, 44444], "
      "uInt32List = [3333333333], "
      "uInt64List = [11111111111111111111], "
      "float32List = [5555.5, inf, -inf, nan], "
      "float64List = [7777.75, inf, -inf, nan], "
      "textList = [\"plugh\", \"xyzzy\", \"thud\"], "
      "dataList = [\"oops\", \"exhausted\", \"rfc3092\"], "
      "structList = [(textField = \"structlist 1\"), "
                    "(textField = \"structlist 2\"), "
                    "(textField = \"structlist 3\")], "
      "enumList = [foo, garply])",
      root.debugString().cStr());
}

TEST(Stringify, MoreValues) {
  EXPECT_STREQ("123", stringify(123).cStr());
  EXPECT_STREQ("1.23e47", stringify(123e45).cStr());
  EXPECT_STREQ("\"foo\"", stringify("foo").cStr());
  EXPECT_STREQ("\"\\a\\b\\n\\t\\\"\"", stringify("\a\b\n\t\"").cStr());

  EXPECT_STREQ("foo", stringify(TestEnum::FOO).cStr());
  EXPECT_STREQ("123", stringify(static_cast<TestEnum>(123)).cStr());
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
