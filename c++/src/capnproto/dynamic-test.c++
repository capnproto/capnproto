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
#include "dynamic.h"
#include "message.h"
#include "logging.h"
#include <gtest/gtest.h>
#include "test-util.h"

namespace capnproto {
namespace internal {
namespace {

void dynamicInitTestmessage(DynamicStruct::Builder builder) {
  builder.set("voidField", Void::VOID);
  builder.set("boolField", true);
  builder.set("int8Field", -123);
  builder.set("int16Field", -12345);
  builder.set("int32Field", -12345678);
  builder.set("int64Field", -123456789012345ll);
  builder.set("uInt8Field", 234u);
  builder.set("uInt16Field", 45678u);
  builder.set("uInt32Field", 3456789012u);
  builder.set("uInt64Field", 12345678901234567890ull);
  builder.set("float32Field", 1234.5);
  builder.set("float64Field", -123e45);
  builder.set("textField", Text::Reader("foo"));
  builder.set("dataField", Data::Reader("bar"));
  {
    auto subBuilder = builder.init("structField").as<DynamicStruct>();
    subBuilder.set("voidField", Void::VOID);
    subBuilder.set("boolField", true);
    subBuilder.set("int8Field", -12);
    subBuilder.set("int16Field", 3456);
    subBuilder.set("int32Field", -78901234);
    subBuilder.set("int64Field", 56789012345678ll);
    subBuilder.set("uInt8Field", 90u);
    subBuilder.set("uInt16Field", 1234u);
    subBuilder.set("uInt32Field", 56789012u);
    subBuilder.set("uInt64Field", 345678901234567890ull);
    subBuilder.set("float32Field", -1.25e-10);
    subBuilder.set("float64Field", 345);
    subBuilder.set("textField", Text::Reader("baz"));
    subBuilder.set("dataField", Data::Reader("qux"));
    {
      auto subSubBuilder = subBuilder.init("structField").as<DynamicStruct>();
      subSubBuilder.set("textField", Text::Reader("nested"));
      subSubBuilder.init("structField").as<DynamicStruct>().set("textField", Text::Reader("really nested"));
    }
    subBuilder.set("enumField", toDynamic(TestEnum::BAZ));

    subBuilder.set("voidList", {Void::VOID, Void::VOID, Void::VOID});
    subBuilder.set("boolList", {false, true, false, true, true});
    subBuilder.set("int8List", {12, -34, -0x80, 0x7f});
    subBuilder.set("int16List", {1234, -5678, -0x8000, 0x7fff});
    subBuilder.set("int32List", {12345678, -90123456, -0x8000000, 0x7ffffff});
    // gcc warns on -0x800...ll and the only work-around I could find was to do -0x7ff...ll-1.
    subBuilder.set("int64List", {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    subBuilder.set("uInt8List", {12u, 34u, 0u, 0xffu});
    subBuilder.set("uInt16List", {1234u, 5678u, 0u, 0xffffu});
    subBuilder.set("uInt32List", {12345678u, 90123456u, 0u, 0xffffffffu});
    subBuilder.set("uInt64List", {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    subBuilder.set("float32List", {0, 1234567, 1e37, -1e37, 1e-37, -1e-37});
    subBuilder.set("float64List", {0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306});
    subBuilder.set("textList", {Text::Reader("quux"), Text::Reader("corge"), Text::Reader("grault")});
    subBuilder.set("dataList", {Data::Reader("garply"), Data::Reader("waldo"), Data::Reader("fred")});
    {
      auto listBuilder = subBuilder.init("structList", 3).as<DynamicList>();
      listBuilder[0].as<DynamicStruct>().set("textField", Text::Reader("x structlist 1"));
      listBuilder[1].as<DynamicStruct>().set("textField", Text::Reader("x structlist 2"));
      listBuilder[2].as<DynamicStruct>().set("textField", Text::Reader("x structlist 3"));
    }
    subBuilder.set("enumList", {toDynamic(TestEnum::QUX),
                                toDynamic(TestEnum::BAR),
                                toDynamic(TestEnum::GRAULT)});
  }
  builder.set("enumField", toDynamic(TestEnum::CORGE));

  builder.init("voidList", 6);
  builder.set("boolList", {true, false, false, true});
  builder.set("int8List", {111, -111});
  builder.set("int16List", {11111, -11111});
  builder.set("int32List", {111111111, -111111111});
  builder.set("int64List", {1111111111111111111ll, -1111111111111111111ll});
  builder.set("uInt8List", {111u, 222u});
  builder.set("uInt16List", {33333u, 44444u});
  builder.set("uInt32List", {3333333333u});
  builder.set("uInt64List", {11111111111111111111ull});
  builder.set("float32List", {5555.5,
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()});
  builder.set("float64List", {7777.75,
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()});
  builder.set("textList", {Text::Reader("plugh"), Text::Reader("xyzzy"), Text::Reader("thud")});
  builder.set("dataList", {Data::Reader("oops"), Data::Reader("exhausted"), Data::Reader("rfc3092")});
  {
    auto listBuilder = builder.init("structList", 3).as<DynamicList>();
    listBuilder[0].as<DynamicStruct>().set("textField", Text::Reader("structlist 1"));
    listBuilder[1].as<DynamicStruct>().set("textField", Text::Reader("structlist 2"));
    listBuilder[2].as<DynamicStruct>().set("textField", Text::Reader("structlist 3"));
  }
  builder.set("enumList", {toDynamic(TestEnum::FOO), toDynamic(TestEnum::GARPLY)});
}

TEST(DynamicApi, Struct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  dynamicInitTestmessage(toDynamic(root));
  checkTestMessage(root);
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
