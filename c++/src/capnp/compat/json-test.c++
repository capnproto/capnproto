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
#include <capnp/compat/json.capnp.h>
#include <kj/debug.h>
#include <kj/string.h>
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
    "  \"float32List\": [5555.5, null, null, null],\n"
    "  \"float64List\": [7777.75, null, null, null],\n"
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

KJ_TEST("basic json decoding") {
  // TODO(cleanup): this test is a mess!
  JsonCodec json;
  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();
    json.decodeRaw("null", root);

    KJ_EXPECT(root.which() == JsonValue::NULL_);
    KJ_EXPECT(root.getNull() == VOID);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("false", root);
    KJ_EXPECT(root.which() == JsonValue::BOOLEAN);
    KJ_EXPECT(root.getBoolean() == false);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("true", root);
    KJ_EXPECT(root.which() == JsonValue::BOOLEAN);
    KJ_EXPECT(root.getBoolean() == true);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("\"foo\"", root);
    KJ_EXPECT(root.which() == JsonValue::STRING);
    KJ_EXPECT(kj::str("foo") == root.getString());
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(R"("\"")", root);
    KJ_EXPECT(root.which() == JsonValue::STRING);
    KJ_EXPECT(kj::str("\"") == root.getString());
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(R"("\\abc\"d\\e")", root);
    KJ_EXPECT(root.which() == JsonValue::STRING);
    KJ_EXPECT(kj::str("\\abc\"d\\e") == root.getString());
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(R"("\"\\\/\b\f\n\r\t\u0003abc\u0064\u0065f")", root);
    KJ_EXPECT(root.which() == JsonValue::STRING);
    KJ_EXPECT(kj::str("\"\\/\b\f\n\r\t\x03""abcdef") == root.getString(), root.getString());
  }
  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("[]", root);
    KJ_EXPECT(root.which() == JsonValue::ARRAY, (uint)root.which());
    KJ_EXPECT(root.getArray().size() == 0);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("[true]", root);
    KJ_EXPECT(root.which() == JsonValue::ARRAY);
    auto array = root.getArray();
    KJ_EXPECT(array.size() == 1, array.size());
    KJ_EXPECT(root.getArray()[0].which() == JsonValue::BOOLEAN);
    KJ_EXPECT(root.getArray()[0].getBoolean() == true);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("  [  true  , false\t\n , null]", root);
    KJ_EXPECT(root.which() == JsonValue::ARRAY);
    auto array = root.getArray();
    KJ_EXPECT(array.size() == 3);
    KJ_EXPECT(array[0].which() == JsonValue::BOOLEAN);
    KJ_EXPECT(array[0].getBoolean() == true);
    KJ_EXPECT(array[1].which() == JsonValue::BOOLEAN);
    KJ_EXPECT(array[1].getBoolean() == false);
    KJ_EXPECT(array[2].which() == JsonValue::NULL_);
    KJ_EXPECT(array[2].getNull() == VOID);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("{}", root);
    KJ_EXPECT(root.which() == JsonValue::OBJECT, (uint)root.which());
    KJ_EXPECT(root.getObject().size() == 0);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(R"({"some": null})", root);
    KJ_EXPECT(root.which() == JsonValue::OBJECT, (uint)root.which());
    auto object = root.getObject();
    KJ_EXPECT(object.size() == 1);
    KJ_EXPECT(kj::str("some") == object[0].getName());
    KJ_EXPECT(object[0].getValue().which() == JsonValue::NULL_);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(R"({"foo\n\tbaz": "a val", "bar": ["a", -5.5e11,  { "z": {}}]})", root);
    KJ_EXPECT(root.which() == JsonValue::OBJECT, (uint)root.which());
    auto object = root.getObject();
    KJ_EXPECT(object.size() == 2);
    KJ_EXPECT(kj::str("foo\n\tbaz") == object[0].getName());
    KJ_EXPECT(object[0].getValue().which() == JsonValue::STRING);
    KJ_EXPECT(kj::str("a val") == object[0].getValue().getString());

    KJ_EXPECT(kj::str("bar") == object[1].getName());
    KJ_EXPECT(object[1].getValue().which() == JsonValue::ARRAY);
    auto array = object[1].getValue().getArray();
    KJ_EXPECT(array.size() == 3, array.size());
    KJ_EXPECT(array[0].which() == JsonValue::STRING);
    KJ_EXPECT(kj::str("a") == array[0].getString());
    KJ_EXPECT(array[1].which() == JsonValue::NUMBER);
    KJ_EXPECT(array[1].getNumber() == -5.5e11);
    KJ_EXPECT(array[2].which() == JsonValue::OBJECT);
    KJ_EXPECT(array[2].getObject().size() == 1);
    KJ_EXPECT(array[2].getObject()[0].getValue().which() == JsonValue::OBJECT);
    KJ_EXPECT(array[2].getObject()[0].getValue().getObject().size() == 0);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("123", root);
    KJ_EXPECT(root.which() == JsonValue::NUMBER);
    KJ_EXPECT(root.getNumber() == 123);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("input", json.decodeRaw("z", root));
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    // Leading + not allowed in numbers.
    KJ_EXPECT_THROW_MESSAGE("Unexpected", json.decodeRaw("+123", root));
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("Overflow", json.decodeRaw("1e1024", root));
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("Underflow", json.decodeRaw("1e-1023", root));
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("Unexpected", json.decodeRaw("[00]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected", json.decodeRaw("[01]", root));
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw("-", root));
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("-5", root);
    KJ_EXPECT(root.which() == JsonValue::NUMBER);
    KJ_EXPECT(root.getNumber() == -5);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw("-5.5", root);
    KJ_EXPECT(root.which() == JsonValue::NUMBER);
    KJ_EXPECT(root.getNumber() == -5.5);
  }

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("a", root));
    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw("[", root));
    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw("{", root));
    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw("\"\\u\"", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[}", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("{]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[}]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[1, , ]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[,]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[true,]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[, 1]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[1\"\"]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("[1,, \"\"]", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("{\"a\"1: 0}", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw(R"({"some": null,})", root));
    KJ_EXPECT_THROW_MESSAGE("Input remains", json.decodeRaw("11a", root));
    KJ_EXPECT_THROW_MESSAGE("Invalid escape", json.decodeRaw(R"("\z")", root));
    KJ_EXPECT_THROW_MESSAGE("Invalid escape", json.decodeRaw(R"("\z")", root));
    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw(R"(["\n\", 3])", root));
    KJ_EXPECT_THROW_MESSAGE("Invalid hex", json.decodeRaw(R"("\u12zz")", root));
    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw("-", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("--", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("1.", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("1.e3", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("1.3e", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("1.3e+", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("1.3e-", root));
  }
}

KJ_TEST("maximum nesting depth") {
  JsonCodec json;
  auto input = kj::str(R"({"foo": "a", "bar": ["b", { "baz": [-5.5e11] }, [ [ 1 ], {  "z": 2 }]]})");
  // `input` has a maximum nesting depth of 4, reached 3 times.

  {
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(input, root);
  }

  {
    json.setMaxNestingDepth(0);
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("nest",
        json.decodeRaw(input, root));
  }

  {
    json.setMaxNestingDepth(3);
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    KJ_EXPECT_THROW_MESSAGE("nest",
        json.decodeRaw(input, root));
  }

  {
    json.setMaxNestingDepth(4);
    MallocMessageBuilder message;
    auto root = message.initRoot<JsonValue>();

    json.decodeRaw(input, root);
  }
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
