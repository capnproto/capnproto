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
#include <math.h>    // for HUGEVAL to check for overflow in strtod
#include <stdlib.h>  // strtod
#include <errno.h>   // for strtod errors
#include <unordered_map>
#include <capnp/orphan.h>
#include <kj/debug.h>
#include <kj/function.h>
#include <kj/vector.h>

namespace capnp {

namespace {

struct TypeHash {
  size_t operator()(const Type& type) const {
    return type.hashCode();
  }
};

struct FieldHash {
  size_t operator()(const StructSchema::Field& field) const {
    return field.getIndex() ^ field.getContainingStruct().getProto().getId();
  }
};

}  // namespace

struct JsonCodec::Impl {
  bool prettyPrint = false;
  size_t maxNestingDepth = 64;

  std::unordered_map<Type, HandlerBase*, TypeHash> typeHandlers;
  std::unordered_map<StructSchema::Field, HandlerBase*, FieldHash> fieldHandlers;

  kj::StringTree encodeRaw(JsonValue::Reader value, uint indent, bool& multiline,
                           bool hasPrefix) const {
    switch (value.which()) {
      case JsonValue::NULL_:
        return kj::strTree("null");
      case JsonValue::BOOLEAN:
        return kj::strTree(value.getBoolean());
      case JsonValue::NUMBER:
        return kj::strTree(value.getNumber());

      case JsonValue::STRING:
        return kj::strTree(encodeString(value.getString()));

      case JsonValue::ARRAY: {
        auto array = value.getArray();
        uint subIndent = indent + (array.size() > 1);
        bool childMultiline = false;
        auto encodedElements = KJ_MAP(element, array) {
          return encodeRaw(element, subIndent, childMultiline, false);
        };

        return kj::strTree('[', encodeList(
            kj::mv(encodedElements), childMultiline, indent, multiline, hasPrefix), ']');
      }

      case JsonValue::OBJECT: {
        auto object = value.getObject();
        uint subIndent = indent + (object.size() > 1);
        bool childMultiline = false;
        kj::StringPtr colon = prettyPrint ? ": " : ":";
        auto encodedElements = KJ_MAP(field, object) {
          return kj::strTree(
              encodeString(field.getName()), colon,
              encodeRaw(field.getValue(), subIndent, childMultiline, true));
        };

        return kj::strTree('{', encodeList(
            kj::mv(encodedElements), childMultiline, indent, multiline, hasPrefix), '}');
      }

      case JsonValue::CALL: {
        auto call = value.getCall();
        auto params = call.getParams();
        uint subIndent = indent + (params.size() > 1);
        bool childMultiline = false;
        auto encodedElements = KJ_MAP(element, params) {
          return encodeRaw(element, subIndent, childMultiline, false);
        };

        return kj::strTree(call.getFunction(), '(', encodeList(
            kj::mv(encodedElements), childMultiline, indent, multiline, true), ')');
      }
    }

    KJ_FAIL_ASSERT("unknown JsonValue type", static_cast<uint>(value.which()));
  }

  kj::String encodeString(kj::StringPtr chars) const {
    static const char HEXDIGITS[] = "0123456789abcdef";
    kj::Vector<char> escaped(chars.size() + 3);

    escaped.add('"');
    for (char c: chars) {
      switch (c) {
        case '\"': escaped.addAll(kj::StringPtr("\\\"")); break;
        case '\\': escaped.addAll(kj::StringPtr("\\\\")); break;
        case '/' : escaped.addAll(kj::StringPtr("\\/" )); break;
        case '\b': escaped.addAll(kj::StringPtr("\\b")); break;
        case '\f': escaped.addAll(kj::StringPtr("\\f")); break;
        case '\n': escaped.addAll(kj::StringPtr("\\n")); break;
        case '\r': escaped.addAll(kj::StringPtr("\\r")); break;
        case '\t': escaped.addAll(kj::StringPtr("\\t")); break;
        default:
          if (c >= 0 && c < 0x20) {
            escaped.addAll(kj::StringPtr("\\u00"));
            uint8_t c2 = c;
            escaped.add(HEXDIGITS[c2 / 16]);
            escaped.add(HEXDIGITS[c2 % 16]);
          } else {
            escaped.add(c);
          }
          break;
      }
    }
    escaped.add('"');
    escaped.add('\0');

    return kj::String(escaped.releaseAsArray());
  }

  kj::StringTree encodeList(kj::Array<kj::StringTree> elements,
                            bool hasMultilineElement, uint indent, bool& multiline,
                            bool hasPrefix) const {
    size_t maxChildSize = 0;
    for (auto& e: elements) maxChildSize = kj::max(maxChildSize, e.size());

    kj::StringPtr prefix;
    kj::StringPtr delim;
    kj::StringPtr suffix;
    kj::String ownPrefix;
    kj::String ownDelim;
    if (!prettyPrint) {
      // No whitespace.
      delim = ",";
      prefix = "";
      suffix = "";
    } else if ((elements.size() > 1) && (hasMultilineElement || maxChildSize > 50)) {
      // If the array contained any multi-line elements, OR it contained sufficiently long
      // elements, then put each element on its own line.
      auto indentSpace = kj::repeat(' ', (indent + 1) * 2);
      delim = ownDelim = kj::str(",\n", indentSpace);
      multiline = true;
      if (hasPrefix) {
        // We're producing a multi-line list, and the first line has some garbage in front of it.
        // Therefore, move the first element to the next line.
        prefix = ownPrefix = kj::str("\n", indentSpace);
      } else {
        prefix = " ";
      }
      suffix = " ";
    } else {
      // Put everything on one line, but add spacing between elements for legibility.
      delim = ", ";
      prefix = "";
      suffix = "";
    }

    return kj::strTree(prefix, kj::StringTree(kj::mv(elements), delim), suffix);
  }
};

JsonCodec::JsonCodec()
    : impl(kj::heap<Impl>()) {}
JsonCodec::~JsonCodec() noexcept(false) {}

void JsonCodec::setPrettyPrint(bool enabled) { impl->prettyPrint = enabled; }

void JsonCodec::setMaxNestingDepth(size_t maxNestingDepth) {
  impl->maxNestingDepth = maxNestingDepth;
}

kj::String JsonCodec::encode(DynamicValue::Reader value, Type type) const {
  MallocMessageBuilder message;
  auto json = message.getRoot<JsonValue>();
  encode(value, type, json);
  return encodeRaw(json);
}

void JsonCodec::decode(kj::ArrayPtr<const char> input, DynamicStruct::Builder output) const {
  MallocMessageBuilder message;
  auto json = message.getRoot<JsonValue>();
  decodeRaw(input, json);
  decode(json, output);
}

Orphan<DynamicValue> JsonCodec::decode(
    kj::ArrayPtr<const char> input, Type type, Orphanage orphanage) const {
  MallocMessageBuilder message;
  auto json = message.getRoot<JsonValue>();
  decodeRaw(input, json);
  return decode(json, type, orphanage);
}

kj::String JsonCodec::encodeRaw(JsonValue::Reader value) const {
  bool multiline = false;
  return impl->encodeRaw(value, 0, multiline, false).flatten();
}

void JsonCodec::encode(DynamicValue::Reader input, Type type, JsonValue::Builder output) const {
  // TODO(0.7): For interfaces, check for handlers on superclasses, per documentation...
  // TODO(0.7): For branded types, should we check for handlers on the generic?
  // TODO(someday): Allow registering handlers for "all structs", "all lists", etc?
  auto iter = impl->typeHandlers.find(type);
  if (iter != impl->typeHandlers.end()) {
    iter->second->encodeBase(*this, input, output);
    return;
  }

  switch (type.which()) {
    case schema::Type::VOID:
      output.setNull();
      break;
    case schema::Type::BOOL:
      output.setBoolean(input.as<bool>());
      break;
    case schema::Type::INT8:
    case schema::Type::INT16:
    case schema::Type::INT32:
    case schema::Type::UINT8:
    case schema::Type::UINT16:
    case schema::Type::UINT32:
      output.setNumber(input.as<double>());
      break;
    case schema::Type::FLOAT32:
    case schema::Type::FLOAT64:
      {
        double value = input.as<double>();
        // Inf, -inf and NaN are not allowed in the JSON spec. Storing into string.
        if (kj::inf() == value) {
          output.setString("Infinity");
        } else if (-kj::inf() == value) {
          output.setString("-Infinity");
        } else if (kj::isNaN(value)) {
          output.setString("NaN");
        } else {
          output.setNumber(value);
        }
      }
      break;
    case schema::Type::INT64:
      output.setString(kj::str(input.as<int64_t>()));
      break;
    case schema::Type::UINT64:
      output.setString(kj::str(input.as<uint64_t>()));
      break;
    case schema::Type::TEXT:
      output.setString(kj::str(input.as<Text>()));
      break;
    case schema::Type::DATA: {
      // Turn into array of byte values. Yep, this is pretty ugly. People really need to override
      // this with a handler.
      auto bytes = input.as<Data>();
      auto array = output.initArray(bytes.size());
      for (auto i: kj::indices(bytes)) {
        array[i].setNumber(bytes[i]);
      }
      break;
    }
    case schema::Type::LIST: {
      auto list = input.as<DynamicList>();
      auto elementType = type.asList().getElementType();
      auto array = output.initArray(list.size());
      for (auto i: kj::indices(list)) {
        encode(list[i], elementType, array[i]);
      }
      break;
    }
    case schema::Type::ENUM: {
      auto e = input.as<DynamicEnum>();
      KJ_IF_MAYBE(symbol, e.getEnumerant()) {
        output.setString(symbol->getProto().getName());
      } else {
        output.setNumber(e.getRaw());
      }
      break;
    }
    case schema::Type::STRUCT: {
      auto structValue = input.as<capnp::DynamicStruct>();
      auto nonUnionFields = structValue.getSchema().getNonUnionFields();

      KJ_STACK_ARRAY(bool, hasField, nonUnionFields.size(), 32, 128);

      uint fieldCount = 0;
      for (auto i: kj::indices(nonUnionFields)) {
        fieldCount += (hasField[i] = structValue.has(nonUnionFields[i]));
      }

      // We try to write the union field, if any, in proper order with the rest.
      auto which = structValue.which();
      bool unionFieldIsNull = false;

      KJ_IF_MAYBE(field, which) {
        // Even if the union field is null, if it is not the default field of the union then we
        // have to print it anyway.
        unionFieldIsNull = !structValue.has(*field);
        if (field->getProto().getDiscriminantValue() != 0 || !unionFieldIsNull) {
          ++fieldCount;
        } else {
          which = nullptr;
        }
      }

      auto object = output.initObject(fieldCount);

      size_t pos = 0;
      for (auto i: kj::indices(nonUnionFields)) {
        auto field = nonUnionFields[i];
        KJ_IF_MAYBE(unionField, which) {
          if (unionField->getIndex() < field.getIndex()) {
            auto outField = object[pos++];
            outField.setName(unionField->getProto().getName());
            if (unionFieldIsNull) {
              outField.initValue().setNull();
            } else {
              encodeField(*unionField, structValue.get(*unionField), outField.initValue());
            }
            which = nullptr;
          }
        }
        if (hasField[i]) {
          auto outField = object[pos++];
          outField.setName(field.getProto().getName());
          encodeField(field, structValue.get(field), outField.initValue());
        }
      }
      if (which != nullptr) {
        // Union field not printed yet; must be last.
        auto unionField = KJ_ASSERT_NONNULL(which);
        auto outField = object[pos++];
        outField.setName(unionField.getProto().getName());
        if (unionFieldIsNull) {
          outField.initValue().setNull();
        } else {
          encodeField(unionField, structValue.get(unionField), outField.initValue());
        }
      }
      KJ_ASSERT(pos == fieldCount);
      break;
    }
    case schema::Type::INTERFACE:
      KJ_FAIL_REQUIRE("don't know how to JSON-encode capabilities; "
                      "please register a JsonCodec::Handler for this");
    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("don't know how to JSON-encode AnyPointer; "
                      "please register a JsonCodec::Handler for this");
  }
}

void JsonCodec::encodeField(StructSchema::Field field, DynamicValue::Reader input,
                            JsonValue::Builder output) const {
  auto iter = impl->fieldHandlers.find(field);
  if (iter != impl->fieldHandlers.end()) {
    iter->second->encodeBase(*this, input, output);
    return;
  }

  encode(input, field.getType(), output);
}

namespace {

template <typename SetFn, typename DecodeArrayFn, typename DecodeObjectFn>
void decodeField(Type type, JsonValue::Reader value, SetFn setFn, DecodeArrayFn decodeArrayFn,
    DecodeObjectFn decodeObjectFn) {
  // This code relies on conversions in DynamicValue::Reader::as<T>.
  switch(type.which()) {
    case schema::Type::VOID:
      break;
    case schema::Type::BOOL:
      switch (value.which()) {
        case JsonValue::BOOLEAN:
          setFn(value.getBoolean());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected boolean value");
      }
      break;
    case schema::Type::INT8:
    case schema::Type::INT16:
    case schema::Type::INT32:
    case schema::Type::INT64:
      // Relies on range check in DynamicValue::Reader::as<IntType>
      switch (value.which()) {
        case JsonValue::NUMBER:
          setFn(value.getNumber());
          break;
        case JsonValue::STRING:
          setFn(value.getString().parseAs<int64_t>());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected integer value");
      }
      break;
    case schema::Type::UINT8:
    case schema::Type::UINT16:
    case schema::Type::UINT32:
    case schema::Type::UINT64:
      // Relies on range check in DynamicValue::Reader::as<IntType>
      switch (value.which()) {
        case JsonValue::NUMBER:
          setFn(value.getNumber());
          break;
        case JsonValue::STRING:
          setFn(value.getString().parseAs<uint64_t>());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected integer value");
      }
      break;
    case schema::Type::FLOAT32:
    case schema::Type::FLOAT64:
      switch (value.which()) {
        case JsonValue::NULL_:
          setFn(kj::nan());
          break;
        case JsonValue::NUMBER:
          setFn(value.getNumber());
          break;
        case JsonValue::STRING:
          setFn(value.getString().parseAs<double>());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected float value");
      }
      break;
    case schema::Type::TEXT:
      switch (value.which()) {
        case JsonValue::STRING:
          setFn(value.getString());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected text value");
      }
      break;
    case schema::Type::DATA:
      switch (value.which()) {
        case JsonValue::ARRAY: {
          auto array = value.getArray();
          kj::Vector<byte> data(array.size());
          for (auto arrayObject : array) {
            auto x = arrayObject.getNumber();
            KJ_REQUIRE(byte(x) == x, "Number in byte array is not an integer in [0, 255]");
            data.add(byte(x));
          }
          setFn(Data::Reader(data.asPtr()));
          break;
        }
        default:
          KJ_FAIL_REQUIRE("Expected data value");
      }
      break;
    case schema::Type::LIST:
      switch (value.which()) {
        case JsonValue::NULL_:
          // nothing to do
          break;
        case JsonValue::ARRAY:
          decodeArrayFn(value.getArray());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected list value");
      }
      break;
    case schema::Type::ENUM:
      switch (value.which()) {
        case JsonValue::STRING:
          setFn(value.getString());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected enum value");
      }
      break;
    case schema::Type::STRUCT:
      switch (value.which()) {
        case JsonValue::NULL_:
          // nothing to do
          break;
        case JsonValue::OBJECT:
          decodeObjectFn(value.getObject());
          break;
        default:
          KJ_FAIL_REQUIRE("Expected object value");
      }
      break;
    case schema::Type::INTERFACE:
      KJ_FAIL_REQUIRE("don't know how to JSON-decode capabilities; "
                      "JsonCodec::Handler not implemented yet :(");
    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("don't know how to JSON-decode AnyPointer; "
                      "JsonCodec::Handler not implemented yet :(");
  }
}
} // namespace

void JsonCodec::decodeArray(List<JsonValue>::Reader input, DynamicList::Builder output) const {
  KJ_ASSERT(input.size() == output.size(), "Builder was not initialized to input size");
  auto type = output.getSchema().getElementType();
  for (auto i = 0; i < input.size(); i++) {
    decodeField(type, input[i],
        [&](DynamicValue::Reader value) { output.set(i, value); },
        [&](List<JsonValue>::Reader array) {
          decodeArray(array, output.init(i, array.size()).as<DynamicList>());
        },
        [&](List<JsonValue::Field>::Reader object) {
          decodeObject(object, output[i].as<DynamicStruct>());
        });
  }
}

void JsonCodec::decodeObject(List<JsonValue::Field>::Reader input, DynamicStruct::Builder output)
    const {
  for (auto field : input) {
    KJ_IF_MAYBE(fieldSchema, output.getSchema().findFieldByName(field.getName())) {
      decodeField((*fieldSchema).getType(), field.getValue(),
          [&](DynamicValue::Reader value) { output.set(*fieldSchema, value); },
          [&](List<JsonValue>::Reader array) {
            decodeArray(array, output.init(*fieldSchema, array.size()).as<DynamicList>());
          },
          [&](List<JsonValue::Field>::Reader object) {
            decodeObject(object, output.init(*fieldSchema).as<DynamicStruct>());
          });
    } else {
      // Unknown json fields are ignored to allow schema evolution
    }
  }
}

void JsonCodec::decode(JsonValue::Reader input, DynamicStruct::Builder output) const {
  // TODO(0.7): type and field handlers
  switch (input.which()) {
    case JsonValue::OBJECT:
      decodeObject(input.getObject(), output);
      break;
    default:
      KJ_FAIL_REQUIRE("Top level json value must be object");
  };
}

Orphan<DynamicValue> JsonCodec::decode(
    JsonValue::Reader input, Type type, Orphanage orphanage) const {
  // TODO(0.7)
  KJ_FAIL_ASSERT("JSON decode into orphanage not implement yet. :(");
}

// -----------------------------------------------------------------------------

namespace {

class Input {
public:
  Input(kj::ArrayPtr<const char> input) : wrapped(input) {}

  bool exhausted() {
    return wrapped.size() == 0 || wrapped.front() == '\0';
  }

  char nextChar() {
    KJ_REQUIRE(!exhausted(), "JSON message ends prematurely.");
    return wrapped.front();
  }

  void advance(size_t numBytes = 1) {
    KJ_REQUIRE(numBytes <= wrapped.size(), "JSON message ends prematurely.");
    wrapped = kj::arrayPtr(wrapped.begin() + numBytes, wrapped.end());
  }

  void advanceTo(const char *newPos) {
    KJ_REQUIRE(wrapped.begin() <= newPos && newPos < wrapped.end(),
        "JSON message ends prematurely.");
    wrapped = kj::arrayPtr(newPos, wrapped.end());
  }

  kj::ArrayPtr<const char> consume(size_t numBytes = 1) {
    auto originalPos = wrapped.begin();
    advance(numBytes);

    return kj::arrayPtr(originalPos, wrapped.begin());
  }

  void consume(char expected) {
    char current = nextChar();
    KJ_REQUIRE(current == expected, "Unexpected input in JSON message.");

    advance();
  }

  void consume(kj::ArrayPtr<const char> expected) {
    KJ_REQUIRE(wrapped.size() >= expected.size());

    auto prefix = wrapped.slice(0, expected.size());
    KJ_REQUIRE(prefix == expected, "Unexpected input in JSON message.");

    advance(expected.size());
  }

  bool tryConsume(char expected) {
    bool found = !exhausted() && nextChar() == expected;
    if (found) { advance(); }

    return found;
  }

  template <typename Predicate>
  void consumeOne(Predicate&& predicate) {
    char current = nextChar();
    KJ_REQUIRE(predicate(current), "Unexpected input in JSON message.");

    advance();
  }

  template <typename Predicate>
  kj::ArrayPtr<const char> consumeWhile(Predicate&& predicate) {
    auto originalPos = wrapped.begin();
    while (!exhausted() && predicate(nextChar())) { advance(); }

    return kj::arrayPtr(originalPos, wrapped.begin());
  }

  template <typename F>  // Function<void(Input&)>
  kj::ArrayPtr<const char> consumeCustom(F&& f) {
    // Allows consuming in a custom manner without exposing the wrapped ArrayPtr.
    auto originalPos = wrapped.begin();
    f(*this);

    return kj::arrayPtr(originalPos, wrapped.begin());
  }

  void consumeWhitespace() {
    consumeWhile([](char chr) {
      return (
        chr == ' '  ||
        chr == '\n' ||
        chr == '\r' ||
        chr == '\t'
      );
    });
  }


private:
  kj::ArrayPtr<const char> wrapped;

};  // class Input

class Parser {
public:
  Parser(size_t maxNestingDepth, kj::ArrayPtr<const char> input) :
    maxNestingDepth(maxNestingDepth), input(input), nestingDepth(0) {}

  void parseValue(JsonValue::Builder& output) {
    input.consumeWhitespace();
    KJ_DEFER(input.consumeWhitespace());

    KJ_REQUIRE(!input.exhausted(), "JSON message ends prematurely.");

    switch (input.nextChar()) {
      case 'n': input.consume(kj::StringPtr("null"));  output.setNull();         break;
      case 'f': input.consume(kj::StringPtr("false")); output.setBoolean(false); break;
      case 't': input.consume(kj::StringPtr("true"));  output.setBoolean(true);  break;
      case '"': parseString(output); break;
      case '[': parseArray(output);  break;
      case '{': parseObject(output); break;
      case '-': case '0': case '1': case '2': case '3':
      case '4': case '5': case '6': case '7': case '8':
      case '9': parseNumber(output); break;
      default: KJ_FAIL_REQUIRE("Unexpected input in JSON message.");
    }
  }

  void parseNumber(JsonValue::Builder& output) {
    auto numberStr = consumeNumber();
    char *endPtr;

    errno = 0;
    double value = strtod(numberStr.begin(), &endPtr);

    KJ_ASSERT(endPtr != numberStr.begin(), "strtod should not fail! Is consumeNumber wrong?");
    KJ_REQUIRE((value != HUGE_VAL && value != -HUGE_VAL) || errno != ERANGE,
        "Overflow in JSON number.");
    KJ_REQUIRE(value != 0.0 || errno != ERANGE,
        "Underflow in JSON number.");

    output.setNumber(value);
  }

  void parseString(JsonValue::Builder& output) {
    output.setString(consumeQuotedString());
  }

  void parseArray(JsonValue::Builder& output) {
    // TODO(perf): Using orphans leaves holes in the message. It's expected
    // that a JsonValue is used for interop, and won't be sent or written as a
    // Cap'n Proto message.  This also applies to parseObject below.
    kj::Vector<Orphan<JsonValue>> values;
    auto orphanage = Orphanage::getForMessageContaining(output);
    bool expectComma = false;

    input.consume('[');
    KJ_REQUIRE(++nestingDepth <= maxNestingDepth, "JSON message nested too deeply.");
    KJ_DEFER(--nestingDepth);

    while (input.consumeWhitespace(), input.nextChar() != ']') {
      auto orphan = orphanage.newOrphan<JsonValue>();
      auto builder = orphan.get();

      if (expectComma) {
        input.consumeWhitespace();
        input.consume(',');
        input.consumeWhitespace();
      }

      parseValue(builder);
      values.add(kj::mv(orphan));

      expectComma = true;
    }

    output.initArray(values.size());
    auto array = output.getArray();

    for (auto i : kj::indices(values)) {
      array.adoptWithCaveats(i, kj::mv(values[i]));
    }

    input.consume(']');
  }

  void parseObject(JsonValue::Builder& output) {
    kj::Vector<Orphan<JsonValue::Field>> fields;
    auto orphanage = Orphanage::getForMessageContaining(output);
    bool expectComma = false;

    input.consume('{');
    KJ_REQUIRE(++nestingDepth <= maxNestingDepth, "JSON message nested too deeply.");
    KJ_DEFER(--nestingDepth);

    while (input.consumeWhitespace(), input.nextChar() != '}') {
      auto orphan = orphanage.newOrphan<JsonValue::Field>();
      auto builder = orphan.get();

      if (expectComma) {
        input.consumeWhitespace();
        input.consume(',');
        input.consumeWhitespace();
      }

      builder.setName(consumeQuotedString());

      input.consumeWhitespace();
      input.consume(':');
      input.consumeWhitespace();

      auto valueBuilder = builder.getValue();
      parseValue(valueBuilder);

      fields.add(kj::mv(orphan));

      expectComma = true;
    }

    output.initObject(fields.size());
    auto object = output.getObject();

    for (auto i : kj::indices(fields)) {
      object.adoptWithCaveats(i, kj::mv(fields[i]));
    }

    input.consume('}');
  }

  bool inputExhausted() { return input.exhausted(); }

private:
  kj::String consumeQuotedString() {
    input.consume('"');
    // TODO(perf): Avoid copy / alloc if no escapes encoutered.
    // TODO(perf): Get statistics on string size and preallocate?
    kj::Vector<char> decoded;

    do {
      auto stringValue = input.consumeWhile([](const char chr) {
          return chr != '"' && chr != '\\';
      });

      decoded.addAll(stringValue);

      if (input.nextChar() == '\\') {  // handle escapes.
        input.advance();
        switch(input.nextChar()) {
          case '"' : decoded.add('"' ); input.advance(); break;
          case '\\': decoded.add('\\'); input.advance(); break;
          case '/' : decoded.add('/' ); input.advance(); break;
          case 'b' : decoded.add('\b'); input.advance(); break;
          case 'f' : decoded.add('\f'); input.advance(); break;
          case 'n' : decoded.add('\n'); input.advance(); break;
          case 'r' : decoded.add('\r'); input.advance(); break;
          case 't' : decoded.add('\t'); input.advance(); break;
          case 'u' :
            input.consume('u');
            unescapeAndAppend(input.consume(size_t(4)), decoded);
            break;
          default: KJ_FAIL_REQUIRE("Invalid escape in JSON string."); break;
        }
      }

    } while(input.nextChar() != '"');

    input.consume('"');
    decoded.add('\0');

    // TODO(perf): This copy can be eliminated, but I can't find the kj::wayToDoIt();
    return kj::String(decoded.releaseAsArray());
  }

  kj::String consumeNumber() {
    auto numArrayPtr = input.consumeCustom([](Input& input) {
      input.tryConsume('-');
      if (!input.tryConsume('0')) {
        input.consumeOne([](char c) { return '1' <= c && c <= '9'; });
        input.consumeWhile([](char c) { return '0' <= c && c <= '9'; });
      }

      if (input.tryConsume('.')) {
        input.consumeWhile([](char c) { return '0' <= c && c <= '9'; });
      }

      if (input.tryConsume('e') || input.tryConsume('E')) {
        input.tryConsume('+') || input.tryConsume('-');
        input.consumeWhile([](char c) { return '0' <= c && c <= '9'; });
      }
    });

    KJ_REQUIRE(numArrayPtr.size() > 0, "Expected number in JSON input.");

    kj::Vector<char> number;
    number.addAll(numArrayPtr);
    number.add('\0');

    return kj::String(number.releaseAsArray());
  }

  // TODO(someday): This "interface" is ugly, and won't work if/when surrogates are handled.
  void unescapeAndAppend(kj::ArrayPtr<const char> hex, kj::Vector<char>& target) {
    KJ_REQUIRE(hex.size() == 4);
    int codePoint = 0;

    for (int i = 0; i < 4; ++i) {
      char c = hex[i];
      codePoint <<= 4;

      if ('0' <= c && c <= '9') {
        codePoint |= c - '0';
      } else if ('a' <= c && c <= 'f') {
        codePoint |= c - 'a';
      } else if ('A' <= c && c <= 'F') {
        codePoint |= c - 'A';
      } else {
        KJ_FAIL_REQUIRE("Invalid hex digit in unicode escape.", c);
      }
    }

    // TODO(0.7): Support at least basic multi-lingual plane, ie ignore surrogates.
    KJ_REQUIRE(codePoint < 128, "non-ASCII unicode escapes are not supported (yet!)");
    target.add(0x7f & static_cast<char>(codePoint));
  }

  const size_t maxNestingDepth;
  Input input;
  size_t nestingDepth;


};  // class Parser

}  // namespace


void JsonCodec::decodeRaw(kj::ArrayPtr<const char> input, JsonValue::Builder output) const {
  Parser parser(impl->maxNestingDepth, input);
  parser.parseValue(output);

  KJ_REQUIRE(parser.inputExhausted(), "Input remains after parsing JSON.");
}

// -----------------------------------------------------------------------------

Orphan<DynamicValue> JsonCodec::HandlerBase::decodeBase(
    const JsonCodec& codec, JsonValue::Reader input, Type type, Orphanage orphanage) const {
  KJ_FAIL_ASSERT("JSON decoder handler type / value type mismatch");
}
void JsonCodec::HandlerBase::decodeStructBase(
    const JsonCodec& codec, JsonValue::Reader input, DynamicStruct::Builder output) const {
  KJ_FAIL_ASSERT("JSON decoder handler type / value type mismatch");
}

void JsonCodec::addTypeHandlerImpl(Type type, HandlerBase& handler) {
  impl->typeHandlers[type] = &handler;
}

void JsonCodec::addFieldHandlerImpl(StructSchema::Field field, Type type, HandlerBase& handler) {
  KJ_REQUIRE(type == field.getType(),
      "handler type did not match field type for addFieldHandler()");
  impl->fieldHandlers[field] = &handler;
}

} // namespace capnp
