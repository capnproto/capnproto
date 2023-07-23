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
#include <capnp/compat/json-test.capnp.h>
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
    "  \"float32List\": [5555.5, \"Infinity\", \"-Infinity\", \"NaN\"],\n"
    "  \"float64List\": [7777.75, \"Infinity\", \"-Infinity\", \"NaN\"],\n"
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

KJ_TEST("decode all types") {
  JsonCodec json;
  json.setHasMode(HasMode::NON_DEFAULT);

#define CASE_MAYBE_ROUNDTRIP(s, f, roundtrip) \
  { \
    MallocMessageBuilder message; \
    auto root = message.initRoot<TestAllTypes>(); \
    kj::StringPtr input = s; \
    json.decode(input, root); \
    KJ_EXPECT((f), input, root); \
    auto reencoded = json.encode(root); \
    KJ_EXPECT(roundtrip == (input == reencoded), roundtrip, input, reencoded); \
  }
#define CASE_NO_ROUNDTRIP(s, f) CASE_MAYBE_ROUNDTRIP(s, f, false)
#define CASE(s, f) CASE_MAYBE_ROUNDTRIP(s, f, true)
#define CASE_THROW(s, errorMessage) \
  { \
    MallocMessageBuilder message; \
    auto root = message.initRoot<TestAllTypes>(); \
    KJ_EXPECT_THROW_MESSAGE(errorMessage, json.decode(s, root)); \
  }
#define CASE_THROW_RECOVERABLE(s, errorMessage) \
  { \
    MallocMessageBuilder message; \
    auto root = message.initRoot<TestAllTypes>(); \
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(errorMessage, json.decode(s, root)); \
  }

  CASE(R"({})", root.getBoolField() == false);
  CASE_NO_ROUNDTRIP(R"({"unknownField":7})", root.getBoolField() == false);
  CASE(R"({"boolField":true})", root.getBoolField() == true);
  CASE(R"({"int8Field":-128})", root.getInt8Field() == -128);
  CASE_NO_ROUNDTRIP(R"({"int8Field":"127"})", root.getInt8Field() == 127);
  CASE_THROW_RECOVERABLE(R"({"int8Field":"-129"})", "Value out-of-range");
  CASE_THROW_RECOVERABLE(R"({"int8Field":128})", "Value out-of-range");
  CASE(R"({"int16Field":-32768})", root.getInt16Field() == -32768);
  CASE_NO_ROUNDTRIP(R"({"int16Field":"32767"})", root.getInt16Field() == 32767);
  CASE_THROW_RECOVERABLE(R"({"int16Field":"-32769"})", "Value out-of-range");
  CASE_THROW_RECOVERABLE(R"({"int16Field":32768})", "Value out-of-range");
  CASE(R"({"int32Field":-2147483648})", root.getInt32Field() == -2147483648);
  CASE_NO_ROUNDTRIP(R"({"int32Field":"2147483647"})", root.getInt32Field() == 2147483647);
  CASE_NO_ROUNDTRIP(R"({"int64Field":-9007199254740992})", root.getInt64Field() == -9007199254740992LL);
  CASE_NO_ROUNDTRIP(R"({"int64Field":9007199254740991})", root.getInt64Field() == 9007199254740991LL);
  CASE(R"({"int64Field":"-9223372036854775808"})", root.getInt64Field() == -9223372036854775808ULL);
  CASE(R"({"int64Field":"9223372036854775807"})", root.getInt64Field() == 9223372036854775807LL);
  CASE_THROW_RECOVERABLE(R"({"int64Field":"-9223372036854775809"})", "Value out-of-range");
  CASE_THROW_RECOVERABLE(R"({"int64Field":"9223372036854775808"})", "Value out-of-range");
  CASE(R"({"uInt8Field":255})", root.getUInt8Field() == 255);
  CASE_NO_ROUNDTRIP(R"({"uInt8Field":"0"})", root.getUInt8Field() == 0);
  CASE_THROW_RECOVERABLE(R"({"uInt8Field":"256"})", "Value out-of-range");
  CASE_THROW_RECOVERABLE(R"({"uInt8Field":-1})", "Value out-of-range");
  CASE(R"({"uInt16Field":65535})", root.getUInt16Field() == 65535);
  CASE_NO_ROUNDTRIP(R"({"uInt16Field":"0"})", root.getUInt16Field() == 0);
  CASE_THROW_RECOVERABLE(R"({"uInt16Field":"655356"})", "Value out-of-range");
  CASE_THROW_RECOVERABLE(R"({"uInt16Field":-1})", "Value out-of-range");
  CASE(R"({"uInt32Field":4294967295})", root.getUInt32Field() == 4294967295);
  CASE_NO_ROUNDTRIP(R"({"uInt32Field":"0"})", root.getUInt32Field() == 0);
  CASE_THROW_RECOVERABLE(R"({"uInt32Field":"42949672956"})", "Value out-of-range");
  CASE_THROW_RECOVERABLE(R"({"uInt32Field":-1})", "Value out-of-range");
  CASE_NO_ROUNDTRIP(R"({"uInt64Field":9007199254740991})", root.getUInt64Field() == 9007199254740991ULL);
  CASE(R"({"uInt64Field":"18446744073709551615"})", root.getUInt64Field() == 18446744073709551615ULL);
  CASE_NO_ROUNDTRIP(R"({"uInt64Field":"0"})", root.getUInt64Field() == 0);
  CASE_THROW_RECOVERABLE(R"({"uInt64Field":"18446744073709551616"})", "Value out-of-range");
  CASE_NO_ROUNDTRIP(R"({"float32Field":0})", root.getFloat32Field() == 0);
  CASE(R"({"float32Field":4.5})", root.getFloat32Field() == 4.5);
  CASE_NO_ROUNDTRIP(R"({"float32Field":null})", kj::isNaN(root.getFloat32Field()));
  CASE(R"({"float32Field":"NaN"})", kj::isNaN(root.getFloat32Field()));
  CASE_NO_ROUNDTRIP(R"({"float32Field":"nan"})", kj::isNaN(root.getFloat32Field()));
  CASE(R"({"float32Field":"Infinity"})", root.getFloat32Field() == kj::inf());
  CASE(R"({"float32Field":"-Infinity"})", root.getFloat32Field() == -kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float32Field":"infinity"})", root.getFloat32Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float32Field":"-infinity"})", root.getFloat32Field() == -kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float32Field":"INF"})", root.getFloat32Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float32Field":"-INF"})", root.getFloat32Field() == -kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float32Field":1e39})", root.getFloat32Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float32Field":-1e39})", root.getFloat32Field() == -kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":0})", root.getFloat64Field() == 0);
  CASE(R"({"float64Field":4.5})", root.getFloat64Field() == 4.5);
  CASE_NO_ROUNDTRIP(R"({"float64Field":null})", kj::isNaN(root.getFloat64Field()));
  CASE(R"({"float64Field":"NaN"})", kj::isNaN(root.getFloat64Field()));
  CASE_NO_ROUNDTRIP(R"({"float64Field":"nan"})", kj::isNaN(root.getFloat64Field()));
  CASE(R"({"float64Field":"Infinity"})", root.getFloat64Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":"infinity"})", root.getFloat64Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":"-infinity"})", root.getFloat64Field() == -kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":"INF"})", root.getFloat64Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":"-INF"})", root.getFloat64Field() == -kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":1e309})", root.getFloat64Field() == kj::inf());
  CASE_NO_ROUNDTRIP(R"({"float64Field":-1e309})", root.getFloat64Field() == -kj::inf());
  CASE(R"({"textField":"hello"})", kj::str("hello") == root.getTextField());
  CASE(R"({"dataField":[7,0,122]})",
      kj::heapArray<byte>({7,0,122}).asPtr() == root.getDataField());
  CASE(R"({"structField":{}})", root.hasStructField() == true);
  CASE(R"({"structField":{}})", root.getStructField().getBoolField() == false);
  CASE_NO_ROUNDTRIP(R"({"structField":{"boolField":false}})", root.getStructField().getBoolField() == false);
  CASE(R"({"structField":{"boolField":true}})", root.getStructField().getBoolField() == true);
  CASE(R"({"enumField":"bar"})", root.getEnumField() == TestEnum::BAR);

  CASE_NO_ROUNDTRIP(R"({"textField":"foo\u1234bar"})",
      kj::str(u8"foo\u1234bar") == root.getTextField());

  CASE_THROW_RECOVERABLE(R"({"structField":null})", "Expected object value");
  CASE_THROW_RECOVERABLE(R"({"structList":null})", "Expected list value");
  CASE_THROW_RECOVERABLE(R"({"boolList":null})", "Expected list value");
  CASE_THROW_RECOVERABLE(R"({"structList":[null]})", "Expected object value");
  CASE_THROW_RECOVERABLE(R"({"int64Field":"177a"})", "String does not contain valid");
  CASE_THROW_RECOVERABLE(R"({"uInt64Field":"177a"})", "String does not contain valid");
  CASE_THROW_RECOVERABLE(R"({"float64Field":"177a"})", "String does not contain valid");

  CASE(R"({})", root.hasBoolList() == false);
  CASE(R"({"boolList":[]})", root.hasBoolList() == true);
  CASE(R"({"boolList":[]})", root.getBoolList().size() == 0);
  CASE(R"({"boolList":[false]})", root.getBoolList().size() == 1);
  CASE(R"({"boolList":[false]})", root.getBoolList()[0] == false);
  CASE(R"({"boolList":[true]})", root.getBoolList()[0] == true);
  CASE(R"({"int8List":[7]})", root.getInt8List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"int8List":["7"]})", root.getInt8List()[0] == 7);
  CASE(R"({"int16List":[7]})", root.getInt16List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"int16List":["7"]})", root.getInt16List()[0] == 7);
  CASE(R"({"int32List":[7]})", root.getInt32List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"int32List":["7"]})", root.getInt32List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"int64List":[7]})", root.getInt64List()[0] == 7);
  CASE(R"({"int64List":["7"]})", root.getInt64List()[0] == 7);
  CASE(R"({"uInt8List":[7]})", root.getUInt8List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"uInt8List":["7"]})", root.getUInt8List()[0] == 7);
  CASE(R"({"uInt16List":[7]})", root.getUInt16List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"uInt16List":["7"]})", root.getUInt16List()[0] == 7);
  CASE(R"({"uInt32List":[7]})", root.getUInt32List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"uInt32List":["7"]})", root.getUInt32List()[0] == 7);
  CASE_NO_ROUNDTRIP(R"({"uInt64List":[7]})", root.getUInt64List()[0] == 7);
  CASE(R"({"uInt64List":["7"]})", root.getUInt64List()[0] == 7);
  CASE(R"({"float32List":[4.5]})", root.getFloat32List()[0] == 4.5);
  CASE_NO_ROUNDTRIP(R"({"float32List":["4.5"]})", root.getFloat32List()[0] == 4.5);
  CASE_NO_ROUNDTRIP(R"({"float32List":[null]})", kj::isNaN(root.getFloat32List()[0]));
  CASE(R"({"float32List":["NaN"]})", kj::isNaN(root.getFloat32List()[0]));
  CASE(R"({"float32List":["Infinity"]})", root.getFloat32List()[0] == kj::inf());
  CASE(R"({"float32List":["-Infinity"]})", root.getFloat32List()[0] == -kj::inf());
  CASE(R"({"float64List":[4.5]})", root.getFloat64List()[0] == 4.5);
  CASE_NO_ROUNDTRIP(R"({"float64List":["4.5"]})", root.getFloat64List()[0] == 4.5);
  CASE_NO_ROUNDTRIP(R"({"float64List":[null]})", kj::isNaN(root.getFloat64List()[0]));
  CASE(R"({"float64List":["NaN"]})", kj::isNaN(root.getFloat64List()[0]));
  CASE(R"({"float64List":["Infinity"]})", root.getFloat64List()[0] == kj::inf());
  CASE(R"({"float64List":["-Infinity"]})", root.getFloat64List()[0] == -kj::inf());
  CASE(R"({"textList":["hello"]})", kj::str("hello") == root.getTextList()[0]);
  CASE(R"({"dataList":[[7,0,122]]})",
      kj::heapArray<byte>({7,0,122}).asPtr() == root.getDataList()[0]);
  CASE(R"({"structList":[{}]})", root.hasStructList() == true);
  CASE(R"({"structList":[{}]})", root.getStructList()[0].getBoolField() == false);
  CASE_NO_ROUNDTRIP(R"({"structList":[{"boolField":false}]})", root.getStructList()[0].getBoolField() == false);
  CASE(R"({"structList":[{"boolField":true}]})", root.getStructList()[0].getBoolField() == true);
  CASE(R"({"enumList":["bar"]})", root.getEnumList()[0] == TestEnum::BAR);
#undef CASE_MAYBE_ROUNDTRIP
#undef CASE_NO_ROUNDTRIP
#undef CASE
#undef CASE_THROW
#undef CASE_THROW_RECOVERABLE
}

KJ_TEST("decode test message") {
  MallocMessageBuilder message;
  auto root = message.getRoot<TestAllTypes>();
  initTestMessage(root);

  JsonCodec json;
  auto encoded = json.encode(root);

  MallocMessageBuilder decodedMessage;
  auto decodedRoot = decodedMessage.initRoot<TestAllTypes>();
  json.decode(encoded, decodedRoot);

  KJ_EXPECT(root.toString().flatten() == decodedRoot.toString().flatten());
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

    json.decodeRaw("\r\n\t {\r\n\t }\r\n\t ", root);
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
    {
      // TODO(msvc):  This raw string literal currently confuses MSVC's preprocessor, so I hoisted
      // it outside the macro.
      auto f = [&] { json.decodeRaw(R"(["\n\", 3])", root); };
      KJ_EXPECT_THROW_MESSAGE("ends prematurely", f());
    }
    KJ_EXPECT_THROW_MESSAGE("Invalid hex", json.decodeRaw(R"("\u12zz")", root));
    KJ_EXPECT_THROW_MESSAGE("ends prematurely", json.decodeRaw("-", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("--", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("\f{}", root));
    KJ_EXPECT_THROW_MESSAGE("Unexpected input", json.decodeRaw("{\v}", root));
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

class TestCallHandler: public JsonCodec::Handler<Text> {
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

class TestDynamicStructHandler: public JsonCodec::Handler<DynamicStruct> {
public:
  void encode(const JsonCodec& codec, DynamicStruct::Reader input,
              JsonValue::Builder output) const override {
    auto fields = input.getSchema().getFields();
    auto items = output.initArray(fields.size());
    for (auto field: fields) {
      KJ_REQUIRE(field.getIndex() < items.size());
      auto item = items[field.getIndex()];
      if (input.has(field)) {
        codec.encode(input.get(field), field.getType(), item);
      } else {
        item.setNull();
      }
    }
  }

  void decode(const JsonCodec& codec, JsonValue::Reader input,
              DynamicStruct::Builder output) const override {
    auto orphanage = Orphanage::getForMessageContaining(output);
    auto fields = output.getSchema().getFields();
    auto items = input.getArray();
    for (auto field: fields) {
      KJ_REQUIRE(field.getIndex() < items.size());
      auto item = items[field.getIndex()];
      if (!item.isNull()) {
        output.adopt(field, codec.decode(item, field.getType(), orphanage));
      }
    }
  }
};

class TestStructHandler: public JsonCodec::Handler<test::TestOldVersion> {
public:
  void encode(const JsonCodec& codec, test::TestOldVersion::Reader input, JsonValue::Builder output) const override {
    dynamicHandler.encode(codec, input, output);
  }

  void decode(const JsonCodec& codec, JsonValue::Reader input, test::TestOldVersion::Builder output) const override {
    dynamicHandler.decode(codec, input, output);
  }

private:
  TestDynamicStructHandler dynamicHandler;
};

KJ_TEST("register custom encoding handlers") {
  JsonCodec json;

  TestStructHandler structHandler;
  json.addTypeHandler(structHandler);

  // JSON decoder can't parse calls back, so test only encoder here
  TestCallHandler callHandler;
  json.addTypeHandler(callHandler);

  MallocMessageBuilder message;
  auto root = message.getRoot<test::TestOldVersion>();
  root.setOld1(123);
  root.setOld2("foo");

  KJ_EXPECT(json.encode(root) == "[\"123\",Frob(123,\"foo\"),null]");
}

KJ_TEST("register custom roundtrip handler") {
  for (auto i = 1; i <= 2; i++) {
    JsonCodec json;
    TestStructHandler staticHandler;
    TestDynamicStructHandler dynamicHandler;
    kj::String encoded;

    if (i == 1) {
      // first iteration: test with explicit struct handler
      json.addTypeHandler(staticHandler);
    } else {
      // second iteration: same checks, but with DynamicStruct handler
      json.addTypeHandler(StructSchema::from<test::TestOldVersion>(), dynamicHandler);
    }

    {
      MallocMessageBuilder message;
      auto root = message.getRoot<test::TestOldVersion>();
      root.setOld1(123);
      root.initOld3().setOld2("foo");

      encoded = json.encode(root);

      KJ_EXPECT(encoded == "[\"123\",null,[\"0\",\"foo\",null]]");
    }

    {
      MallocMessageBuilder message;
      auto root = message.getRoot<test::TestOldVersion>();
      json.decode(encoded, root);

      KJ_EXPECT(root.getOld1() == 123);
      KJ_EXPECT(!root.hasOld2());
      auto nested = root.getOld3();
      KJ_EXPECT(nested.getOld1() == 0);
      KJ_EXPECT("foo" == nested.getOld2());
      KJ_EXPECT(!nested.hasOld3());
    }
  }
}

KJ_TEST("register field handler") {
  TestStructHandler handler;
  JsonCodec json;
  json.addFieldHandler(StructSchema::from<test::TestOldVersion>().getFieldByName("old3"),
                       handler);

  kj::String encoded;

  {
    MallocMessageBuilder message;
    auto root = message.getRoot<test::TestOldVersion>();
    root.setOld1(123);
    root.setOld2("foo");
    auto nested = root.initOld3();
    nested.setOld2("bar");

    encoded = json.encode(root);

    KJ_EXPECT(encoded == "{\"old1\":\"123\",\"old2\":\"foo\",\"old3\":[\"0\",\"bar\",null]}")
  }

  {
    MallocMessageBuilder message;
    auto root = message.getRoot<test::TestOldVersion>();
    json.decode(encoded, root);

    KJ_EXPECT(root.getOld1() == 123);
    KJ_EXPECT("foo" == root.getOld2());
    auto nested = root.getOld3();
    KJ_EXPECT(nested.getOld1() == 0);
    KJ_EXPECT("bar" == nested.getOld2());
    KJ_EXPECT(!nested.hasOld3());
  }
}

class TestCapabilityHandler: public JsonCodec::Handler<test::TestInterface> {
public:
  void encode(const JsonCodec& codec, test::TestInterface::Client input,
              JsonValue::Builder output) const override {
    KJ_UNIMPLEMENTED("TestCapabilityHandler::encode");
  }

  test::TestInterface::Client decode(
      const JsonCodec& codec, JsonValue::Reader input) const override {
    return nullptr;
  }
};

KJ_TEST("register capability handler") {
  // This test currently only checks that this compiles, which at one point wasn't the caes.
  // TODO(test): Actually run some code here.

  TestCapabilityHandler handler;
  JsonCodec json;
  json.addTypeHandler(handler);
}

static constexpr kj::StringPtr GOLDEN_ANNOTATED =
R"({ "names-can_contain!anything Really": "foo",
  "flatFoo": 123,
  "flatBar": "abc",
  "renamed-flatBaz": {"hello": true},
  "flatQux": "cba",
  "pfx.foo": "this is a long string in order to force multi-line pretty printing",
  "pfx.renamed-bar": 321,
  "pfx.baz": {"hello": true},
  "pfx.xfp.qux": "fed",
  "union-type": "renamed-bar",
  "barMember": 789,
  "multiMember": "ghi",
  "dependency": {"renamed-foo": "corge"},
  "simpleGroup": {"renamed-grault": "garply"},
  "enums": ["qux", "renamed-bar", "foo", "renamed-baz"],
  "innerJson": [123, "hello", {"object": true}],
  "customFieldHandler": "add-prefix-waldo",
  "testBase64": "ZnJlZA==",
  "testHex": "706c756768",
  "bUnion": "renamed-bar",
  "bValue": 678,
  "externalUnion": {"type": "bar", "value": "cba"},
  "unionWithVoid": {"type": "voidValue"} })"_kj;

static constexpr kj::StringPtr GOLDEN_ANNOTATED_REVERSE =
R"({
  "unionWithVoid": {"type": "voidValue"},
  "externalUnion": {"type": "bar", "value": "cba"},
  "bValue": 678,
  "bUnion": "renamed-bar",
  "testHex": "706c756768",
  "testBase64": "ZnJlZA==",
  "customFieldHandler": "add-prefix-waldo",
  "innerJson": [123, "hello", {"object": true}],
  "enums": ["qux", "renamed-bar", "foo", "renamed-baz"],
  "simpleGroup": { "renamed-grault": "garply" },
  "dependency": { "renamed-foo": "corge" },
  "multiMember": "ghi",
  "barMember": 789,
  "union-type": "renamed-bar",
  "pfx.xfp.qux": "fed",
  "pfx.baz": {"hello": true},
  "pfx.renamed-bar": 321,
  "pfx.foo": "this is a long string in order to force multi-line pretty printing",
  "flatQux": "cba",
  "renamed-flatBaz": {"hello": true},
  "flatBar": "abc",
  "flatFoo": 123,
  "names-can_contain!anything Really": "foo"
})"_kj;

class PrefixAdder: public JsonCodec::Handler<capnp::Text> {
public:
  void encode(const JsonCodec& codec, capnp::Text::Reader input, JsonValue::Builder output) const {
    output.setString(kj::str("add-prefix-", input));
  }

  Orphan<capnp::Text> decode(const JsonCodec& codec, JsonValue::Reader input,
                             Orphanage orphanage) const {
    return orphanage.newOrphanCopy(capnp::Text::Reader(input.getString().slice(11)));
  }
};

KJ_TEST("rename fields") {
  JsonCodec json;
  json.handleByAnnotation<TestJsonAnnotations>();
  json.setPrettyPrint(true);

  PrefixAdder customHandler;
  json.addFieldHandler(Schema::from<TestJsonAnnotations>().getFieldByName("customFieldHandler"),
                       customHandler);

  kj::String goldenText;

  {
    MallocMessageBuilder message;
    auto root = message.getRoot<TestJsonAnnotations>();
    root.setSomeField("foo");

    auto aGroup = root.getAGroup();
    aGroup.setFlatFoo(123);
    aGroup.setFlatBar("abc");
    aGroup.getFlatBaz().setHello(true);
    aGroup.getDoubleFlat().setFlatQux("cba");

    auto prefixedGroup = root.getPrefixedGroup();
    prefixedGroup.setFoo("this is a long string in order to force multi-line pretty printing");
    prefixedGroup.setBar(321);
    prefixedGroup.getBaz().setHello(true);
    prefixedGroup.getMorePrefix().setQux("fed");

    auto unionBar = root.getAUnion().initBar();
    unionBar.setBarMember(789);
    unionBar.setMultiMember("ghi");

    root.initDependency().setFoo("corge");
    root.initSimpleGroup().setGrault("garply");

    root.setEnums({
      TestJsonAnnotatedEnum::QUX,
      TestJsonAnnotatedEnum::BAR,
      TestJsonAnnotatedEnum::FOO,
      TestJsonAnnotatedEnum::BAZ
    });

    auto val = root.initInnerJson();
    auto arr = val.initArray(3);
    arr[0].setNumber(123);
    arr[1].setString("hello");
    auto field = arr[2].initObject(1)[0];
    field.setName("object");
    field.initValue().setBoolean(true);

    root.setCustomFieldHandler("waldo");

    root.setTestBase64("fred"_kj.asBytes());
    root.setTestHex("plugh"_kj.asBytes());

    root.getBUnion().setBar(678);

    root.initExternalUnion().initBar().setValue("cba");

    root.initUnionWithVoid().setVoidValue();

    auto encoded = json.encode(root.asReader());
    KJ_EXPECT(encoded == GOLDEN_ANNOTATED, encoded);

    goldenText = kj::str(root);
  }

  {
    MallocMessageBuilder message;
    auto root = message.getRoot<TestJsonAnnotations>();
    json.decode(GOLDEN_ANNOTATED, root);

    KJ_EXPECT(kj::str(root) == goldenText, root, goldenText);
  }

  {
    // Try parsing in reverse, mostly to test that union tags can come after content.
    MallocMessageBuilder message;
    auto root = message.getRoot<TestJsonAnnotations>();
    json.decode(GOLDEN_ANNOTATED_REVERSE, root);

    KJ_EXPECT(kj::str(root) == goldenText, root, goldenText);
  }
}

KJ_TEST("base64 union encoded correctly") {
  // At one point field handlers were not correctly applied when the field was a member of a union
  // in a type that was handled by annotation.

  JsonCodec json;
  json.handleByAnnotation<TestBase64Union>();
  json.setPrettyPrint(true);

  MallocMessageBuilder message;
  auto root = message.getRoot<TestBase64Union>();
  root.initFoo(5);

  KJ_EXPECT(json.encode(root) == "{\"foo\": \"AAAAAAA=\"}", json.encode(root));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
