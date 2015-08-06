// Copyright (c) 2015 Sandstorm Development Group, Inc. and contributors
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

#include "json.h"
#include <capnp/test-util.h>
#include <kj/debug.h>
#include <kj/test.h>

namespace capnp {
namespace _ {  // private
namespace {

KJ_TEST("basic json encoding") {
  JsonCodec json;

  KJ_EXPECT(json.encode(VOID) == "null");
  KJ_EXPECT(json.encode(true) == "true");
  KJ_EXPECT(json.encode(false) == "false");
  KJ_EXPECT(json.encode(123) == "123");
  KJ_EXPECT(json.encode(-5.5) == "-5.5");
  KJ_EXPECT(json.encode(Text::Reader("foo")) == "\"foo\"");
  KJ_EXPECT(json.encode(Text::Reader("ab\"cd\\ef\x03")) == "\"ab\\\"cd\\\\ef\\u0003\"");
  KJ_EXPECT(json.encode(test::TestEnum::CORGE) == "\"corge\"");

  byte bytes[] = {12, 34, 56};
  KJ_EXPECT(json.encode(Data::Reader(bytes, 3)) == "[12,34,56]");

  json.setPrettyPrint(true);
  KJ_EXPECT(json.encode(Data::Reader(bytes, 3)) == "[12, 34, 56]");
}

const char ALL_TYPES_JSON[] =
    "{ \"voidField\": null,\n"
    "  \"boolField\": true,\n"
    "  \"int8Field\": -123,\n"
    "  \"int16Field\": -12345,\n"
    "  \"int32Field\": -12345678,\n"
    "  \"int64Field\": \"-123456789012345\",\n"
    "  \"uInt8Field\": 234,\n"
    "  \"uInt16Field\": 45678,\n"
    "  \"uInt32Field\": 3456789012,\n"
    "  \"uInt64Field\": \"12345678901234567890\",\n"
    "  \"float32Field\": 1234.5,\n"
    "  \"float64Field\": -1.23e47,\n"
    "  \"textField\": \"foo\",\n"
    "  \"dataField\": [98, 97, 114],\n"
    "  \"structField\": {\n"
    "    \"voidField\": null,\n"
    "    \"boolField\": true,\n"
    "    \"int8Field\": -12,\n"
    "    \"int16Field\": 3456,\n"
    "    \"int32Field\": -78901234,\n"
    "    \"int64Field\": \"56789012345678\",\n"
    "    \"uInt8Field\": 90,\n"
    "    \"uInt16Field\": 1234,\n"
    "    \"uInt32Field\": 56789012,\n"
    "    \"uInt64Field\": \"345678901234567890\",\n"
    "    \"float32Field\": -1.2499999646475857e-10,\n"
    "    \"float64Field\": 345,\n"
    "    \"textField\": \"baz\",\n"
    "    \"dataField\": [113, 117, 120],\n"
    "    \"structField\": {\n"
    "      \"voidField\": null,\n"
    "      \"boolField\": false,\n"
    "      \"int8Field\": 0,\n"
    "      \"int16Field\": 0,\n"
    "      \"int32Field\": 0,\n"
    "      \"int64Field\": \"0\",\n"
    "      \"uInt8Field\": 0,\n"
    "      \"uInt16Field\": 0,\n"
    "      \"uInt32Field\": 0,\n"
    "      \"uInt64Field\": \"0\",\n"
    "      \"float32Field\": 0,\n"
    "      \"float64Field\": 0,\n"
    "      \"textField\": \"nested\",\n"
    "      \"structField\": {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"really nested\", \"enumField\": \"foo\", \"interfaceField\": null},\n"
    "      \"enumField\": \"foo\",\n"
    "      \"interfaceField\": null },\n"
    "    \"enumField\": \"baz\",\n"
    "    \"interfaceField\": null,\n"
    "    \"voidList\": [null, null, null],\n"
    "    \"boolList\": [false, true, false, true, true],\n"
    "    \"int8List\": [12, -34, -128, 127],\n"
    "    \"int16List\": [1234, -5678, -32768, 32767],\n"
    "    \"int32List\": [12345678, -90123456, -2147483648, 2147483647],\n"
    "    \"int64List\": [\"123456789012345\", \"-678901234567890\", \"-9223372036854775808\", \"9223372036854775807\"],\n"
    "    \"uInt8List\": [12, 34, 0, 255],\n"
    "    \"uInt16List\": [1234, 5678, 0, 65535],\n"
    "    \"uInt32List\": [12345678, 90123456, 0, 4294967295],\n"
    "    \"uInt64List\": [\"123456789012345\", \"678901234567890\", \"0\", \"18446744073709551615\"],\n"
    "    \"float32List\": [0, 1234567, 9.9999999338158125e36, -9.9999999338158125e36, 9.99999991097579e-38, -9.99999991097579e-38],\n"
    "    \"float64List\": [0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306],\n"
    "    \"textList\": [\"quux\", \"corge\", \"grault\"],\n"
    "    \"dataList\": [[103, 97, 114, 112, 108, 121], [119, 97, 108, 100, 111], [102, 114, 101, 100]],\n"
    "    \"structList\": [\n"
    "      {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"x structlist 1\", \"enumField\": \"foo\", \"interfaceField\": null},\n"
    "      {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"x structlist 2\", \"enumField\": \"foo\", \"interfaceField\": null},\n"
    "      {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"x structlist 3\", \"enumField\": \"foo\", \"interfaceField\": null} ],\n"
    "    \"enumList\": [\"qux\", \"bar\", \"grault\"] },\n"
    "  \"enumField\": \"corge\",\n"
    "  \"interfaceField\": null,\n"
    "  \"voidList\": [null, null, null, null, null, null],\n"
    "  \"boolList\": [true, false, false, true],\n"
    "  \"int8List\": [111, -111],\n"
    "  \"int16List\": [11111, -11111],\n"
    "  \"int32List\": [111111111, -111111111],\n"
    "  \"int64List\": [\"1111111111111111111\", \"-1111111111111111111\"],\n"
    "  \"uInt8List\": [111, 222],\n"
    "  \"uInt16List\": [33333, 44444],\n"
    "  \"uInt32List\": [3333333333],\n"
    "  \"uInt64List\": [\"11111111111111111111\"],\n"
    "  \"float32List\": [5555.5, inf, -inf, nan],\n"
    "  \"float64List\": [7777.75, inf, -inf, nan],\n"
    "  \"textList\": [\"plugh\", \"xyzzy\", \"thud\"],\n"
    "  \"dataList\": [[111, 111, 112, 115], [101, 120, 104, 97, 117, 115, 116, 101, 100], [114, 102, 99, 51, 48, 57, 50]],\n"
    "  \"structList\": [\n"
    "    {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"structlist 1\", \"enumField\": \"foo\", \"interfaceField\": null},\n"
    "    {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"structlist 2\", \"enumField\": \"foo\", \"interfaceField\": null},\n"
    "    {\"voidField\": null, \"boolField\": false, \"int8Field\": 0, \"int16Field\": 0, \"int32Field\": 0, \"int64Field\": \"0\", \"uInt8Field\": 0, \"uInt16Field\": 0, \"uInt32Field\": 0, \"uInt64Field\": \"0\", \"float32Field\": 0, \"float64Field\": 0, \"textField\": \"structlist 3\", \"enumField\": \"foo\", \"interfaceField\": null} ],\n"
    "  \"enumList\": [\"foo\", \"garply\"] }";

KJ_TEST("encode all types") {
  MallocMessageBuilder message;
  auto root = message.getRoot<TestAllTypes>();
  initTestMessage(root);

  JsonCodec json;
  json.setPrettyPrint(true);
  KJ_EXPECT(json.encode(root) == ALL_TYPES_JSON);

  // Verify that if we strip out the non-string spaces, we get the non-pretty-print version.
  kj::Vector<char> chars;
  bool inQuotes = false;
  for (char c: ALL_TYPES_JSON) {
    if (c == '\"') inQuotes = !inQuotes;

    if ((c == '\n' || c == ' ') && !inQuotes) {
      // skip space
    } else {
      chars.add(c);
    }
  }
  kj::String nospaces(chars.releaseAsArray());

  json.setPrettyPrint(false);
  KJ_EXPECT(json.encode(root) == nospaces);
}

KJ_TEST("encode union") {
  MallocMessageBuilder message;
  auto root = message.getRoot<test::TestUnnamedUnion>();

  root.setBefore("a");
  root.setMiddle(44);
  root.setAfter("c");

  JsonCodec json;

  root.setFoo(123);
  KJ_EXPECT(json.encode(root) == "{\"before\":\"a\",\"foo\":123,\"middle\":44,\"after\":\"c\"}");

  root.setBar(321);
  KJ_EXPECT(json.encode(root) == "{\"before\":\"a\",\"middle\":44,\"bar\":321,\"after\":\"c\"}");
}

class TestHandler: public JsonCodec::Handler<Text> {
public:
  void encode(const JsonCodec& codec, Text::Reader input,
              JsonValue::Builder output) const override {
    auto call = output.initCall();
    call.setFunction("Frob");
    auto params = call.initParams(2);
    params[0].setNumber(123);
    params[1].setString(input);
  }

  Orphan<Text> decode(const JsonCodec& codec, JsonValue::Reader input,
                      Orphanage orphanage) const override {
    KJ_UNIMPLEMENTED("TestHandler::decode");
  }
};

KJ_TEST("register handler") {
  MallocMessageBuilder message;
  auto root = message.getRoot<test::TestOldVersion>();

  TestHandler handler;
  JsonCodec json;
  json.addTypeHandler(handler);

  root.setOld1(123);
  root.setOld2("foo");
  KJ_EXPECT(json.encode(root) == "{\"old1\":\"123\",\"old2\":Frob(123,\"foo\")}");
}

KJ_TEST("register field handler") {
  MallocMessageBuilder message;
  auto root = message.getRoot<test::TestOutOfOrder>();

  TestHandler handler;
  JsonCodec json;
  json.addFieldHandler(StructSchema::from<test::TestOutOfOrder>().getFieldByName("corge"),
                       handler);

  root.setBaz("abcd");
  root.setCorge("efg");
  KJ_EXPECT(json.encode(root) == "{\"corge\":Frob(123,\"efg\"),\"baz\":\"abcd\"}");
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
