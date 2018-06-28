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
#include <map>
#include <set>
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
  HasMode hasMode = HasMode::NON_NULL;
  size_t maxNestingDepth = 64;

  std::unordered_map<Type, HandlerBase*, TypeHash> typeHandlers;
  std::unordered_map<StructSchema::Field, HandlerBase*, FieldHash> fieldHandlers;
  std::unordered_map<Type, kj::Maybe<kj::Own<AnnotatedHandler>>, TypeHash> annotatedHandlers;
  std::unordered_map<Type, kj::Own<AnnotatedEnumHandler>, TypeHash> annotatedEnumHandlers;

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

void JsonCodec::setHasMode(HasMode mode) { impl->hasMode = mode; }

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
  // TODO(soon): For interfaces, check for handlers on superclasses, per documentation...
  // TODO(soon): For branded types, should we check for handlers on the generic?
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
        fieldCount += (hasField[i] = structValue.has(nonUnionFields[i], impl->hasMode));
      }

      // We try to write the union field, if any, in proper order with the rest.
      auto which = structValue.which();
      bool unionFieldIsNull = false;

      KJ_IF_MAYBE(field, which) {
        // Even if the union field is null, if it is not the default field of the union then we
        // have to print it anyway.
        unionFieldIsNull = !structValue.has(*field, impl->hasMode);
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

Orphan<DynamicList> JsonCodec::decodeArray(List<JsonValue>::Reader input, ListSchema type, Orphanage orphanage) const {
  auto orphan = orphanage.newOrphan(type, input.size());
  auto output = orphan.get();
  for (auto i: kj::indices(input)) {
    output.adopt(i, decode(input[i], type.getElementType(), orphanage));
  }
  return orphan;
}

void JsonCodec::decodeObject(JsonValue::Reader input, StructSchema type, Orphanage orphanage, DynamicStruct::Builder output) const {
  KJ_REQUIRE(input.isObject(), "Expected object value");
  for (auto field: input.getObject()) {
    KJ_IF_MAYBE(fieldSchema, type.findFieldByName(field.getName())) {
      auto fieldValue = field.getValue();
      auto fieldType = (*fieldSchema).getType();

      auto iter = impl->fieldHandlers.find(*fieldSchema);
      if (iter != impl->fieldHandlers.end()) {
        output.adopt(*fieldSchema, iter->second->decodeBase(*this, fieldValue, fieldType, orphanage));
      } else {
        output.adopt(*fieldSchema, decode(fieldValue, fieldType, orphanage));
      }
    } else {
      // Unknown json fields are ignored to allow schema evolution
    }
  }
}

void JsonCodec::decode(JsonValue::Reader input, DynamicStruct::Builder output) const {
  auto type = output.getSchema();

  auto iter = impl->typeHandlers.find(type);
  if (iter != impl->typeHandlers.end()) {
    return iter->second->decodeStructBase(*this, input, output);
  }

  decodeObject(input, type, Orphanage::getForMessageContaining(output), output);
}

Orphan<DynamicValue> JsonCodec::decode(
    JsonValue::Reader input, Type type, Orphanage orphanage) const {
  auto iter = impl->typeHandlers.find(type);
  if (iter != impl->typeHandlers.end()) {
    return iter->second->decodeBase(*this, input, type, orphanage);
  }

  switch(type.which()) {
    case schema::Type::VOID:
      return capnp::VOID;
    case schema::Type::BOOL:
      switch (input.which()) {
        case JsonValue::BOOLEAN:
          return input.getBoolean();
        default:
          KJ_FAIL_REQUIRE("Expected boolean value");
      }
    case schema::Type::INT8:
    case schema::Type::INT16:
    case schema::Type::INT32:
    case schema::Type::INT64:
      // Relies on range check in DynamicValue::Reader::as<IntType>
      switch (input.which()) {
        case JsonValue::NUMBER:
          return input.getNumber();
        case JsonValue::STRING:
          return input.getString().parseAs<int64_t>();
        default:
          KJ_FAIL_REQUIRE("Expected integer value");
      }
    case schema::Type::UINT8:
    case schema::Type::UINT16:
    case schema::Type::UINT32:
    case schema::Type::UINT64:
      // Relies on range check in DynamicValue::Reader::as<IntType>
      switch (input.which()) {
        case JsonValue::NUMBER:
          return input.getNumber();
        case JsonValue::STRING:
          return input.getString().parseAs<uint64_t>();
        default:
          KJ_FAIL_REQUIRE("Expected integer value");
      }
    case schema::Type::FLOAT32:
    case schema::Type::FLOAT64:
      switch (input.which()) {
        case JsonValue::NULL_:
          return kj::nan();
        case JsonValue::NUMBER:
          return input.getNumber();
        case JsonValue::STRING:
          return input.getString().parseAs<double>();
        default:
          KJ_FAIL_REQUIRE("Expected float value");
      }
    case schema::Type::TEXT:
      switch (input.which()) {
        case JsonValue::STRING:
          return orphanage.newOrphanCopy(input.getString());
        default:
          KJ_FAIL_REQUIRE("Expected text value");
      }
    case schema::Type::DATA:
      switch (input.which()) {
        case JsonValue::ARRAY: {
          auto array = input.getArray();
          auto orphan = orphanage.newOrphan<Data>(array.size());
          auto data = orphan.get();
          for (auto i: kj::indices(array)) {
            auto x = array[i].getNumber();
            KJ_REQUIRE(byte(x) == x, "Number in byte array is not an integer in [0, 255]");
            data[i] = x;
          }
          return kj::mv(orphan);
        }
        default:
          KJ_FAIL_REQUIRE("Expected data value");
      }
    case schema::Type::LIST:
      switch (input.which()) {
        case JsonValue::ARRAY:
          return decodeArray(input.getArray(), type.asList(), orphanage);
        default:
          KJ_FAIL_REQUIRE("Expected list value");
      }
    case schema::Type::ENUM:
      switch (input.which()) {
        case JsonValue::STRING:
          return DynamicEnum(type.asEnum().getEnumerantByName(input.getString()));
        default:
          KJ_FAIL_REQUIRE("Expected enum value");
      }
    case schema::Type::STRUCT: {
      auto structType = type.asStruct();
      auto orphan = orphanage.newOrphan(structType);
      decodeObject(input, structType, orphanage, orphan.get());
      return kj::mv(orphan);
    }
    case schema::Type::INTERFACE:
      KJ_FAIL_REQUIRE("don't know how to JSON-decode capabilities; "
                      "please register a JsonCodec::Handler for this");
    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("don't know how to JSON-decode AnyPointer; "
                      "please register a JsonCodec::Handler for this");
  }

  KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT;
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
    output.setNumber(consumeNumber().parseAs<double>());
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

    // TODO(soon): Support at least basic multi-lingual plane, ie ignore surrogates.
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

// =======================================================================================

static constexpr uint64_t JSON_NAME_ANNOTATION_ID = 0xfa5b1fd61c2e7c3dull;
static constexpr uint64_t JSON_FLATTEN_ANNOTATION_ID = 0x82d3e852af0336bfull;
static constexpr uint64_t JSON_DISCRIMINATOR_ANNOTATION_ID = 0xcfa794e8d19a0162ull;

class JsonCodec::AnnotatedHandler final: public JsonCodec::Handler<DynamicStruct> {
public:
  AnnotatedHandler(JsonCodec& codec, StructSchema schema, kj::Maybe<kj::StringPtr> discriminator,
                   kj::Vector<Schema>& dependencies)
      : schema(schema) {
    auto schemaProto = schema.getProto();
    auto typeName = schemaProto.getDisplayName();

    if (discriminator == nullptr) {
      // There are two cases of unions:
      // * Named unions, which are special cases of named groups. In this case, the union may be
      //   annotated by annotating the field. In this case, we receive a non-null `discriminator`
      //   as a constructor parameter, and schemaProto.getAnnotations() must be empty because
      //   it's not possible to annotate a group's type (becaues the type is anonymous).
      // * Unnamed unions, of which there can only be one in any particular scope. In this case,
      //   the parent struct type itself is annotated.
      // So if we received `null` as the constructor parameter, check for annotations on the struct
      // type.
      for (auto anno: schemaProto.getAnnotations()) {
        switch (anno.getId()) {
          case JSON_DISCRIMINATOR_ANNOTATION_ID:
            discriminator = anno.getValue().getText();
            break;
        }
      }
    }

    KJ_IF_MAYBE(d, discriminator) {
      unionTagName = discriminator;
      fieldsByName.insert(std::make_pair(*d, FieldNameInfo {
        FieldNameInfo::UNION_TAG, 0, 0, nullptr
      }));
    }

    discriminantOffset = schemaProto.getStruct().getDiscriminantOffset();

    fields = KJ_MAP(field, schema.getFields()) {
      auto fieldProto = field.getProto();
      auto type = field.getType();
      auto fieldName = fieldProto.getName();

      FieldNameInfo nameInfo;
      nameInfo.index = field.getIndex();
      nameInfo.type = FieldNameInfo::NORMAL;
      nameInfo.prefixLength = 0;

      FieldInfo info;
      info.name = fieldName;

      kj::Maybe<kj::StringPtr> subDiscriminator;
      bool flattened = false;
      for (auto anno: field.getProto().getAnnotations()) {
        switch (anno.getId()) {
          case JSON_NAME_ANNOTATION_ID:
            info.name = anno.getValue().getText();
            break;
          case JSON_FLATTEN_ANNOTATION_ID:
            KJ_REQUIRE(type.isStruct(), "only struct types can be flattened", fieldName, typeName);
            flattened = true;
            info.prefix = anno.getValue().getStruct().getAs<json::FlattenOptions>().getPrefix();
            break;
          case JSON_DISCRIMINATOR_ANNOTATION_ID:
            KJ_REQUIRE(fieldProto.isGroup(), "only unions can have discriminator");
            subDiscriminator = anno.getValue().getText();
            break;
        }
      }

      if (flattened) {
        info.flattenHandler = codec.loadAnnotatedHandler(
            type.asStruct(), subDiscriminator, dependencies);
      }

      bool isUnionMember = fieldProto.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT;

      KJ_IF_MAYBE(fh, info.flattenHandler) {
        // Set up fieldsByName for each of the child's fields.
        for (auto& entry: fh->fieldsByName) {
          kj::StringPtr flattenedName;
          kj::String ownName;
          if (info.prefix.size() > 0) {
            ownName = kj::str(info.prefix, entry.first);
            flattenedName = ownName;
          } else {
            flattenedName = entry.first;
          }
          fieldsByName.insert(std::make_pair(flattenedName, FieldNameInfo {
            isUnionMember ? FieldNameInfo::FLATTENED_FROM_UNION : FieldNameInfo::FLATTENED,
            field.getIndex(), (uint)info.prefix.size(), kj::mv(ownName)
          }));
        }
      }

      if (!flattened) {
        fieldsByName.insert(std::make_pair(info.name, kj::mv(nameInfo)));
      }

      if (isUnionMember) {
        unionTagValues.insert(std::make_pair(info.name, field));
      }

      // Look for dependencies that we need to add.
      while (type.isList()) type = type.asList().getElementType();
      if (codec.impl->typeHandlers.count(type) == 0) {
        switch (type.which()) {
          case schema::Type::STRUCT:
            dependencies.add(type.asStruct());
            break;
          case schema::Type::ENUM:
            dependencies.add(type.asEnum());
            break;
          case schema::Type::INTERFACE:
            dependencies.add(type.asInterface());
            break;
          default:
            break;
        }
      }

      return info;
    };
  }

  const StructSchema schema;

  void encode(const JsonCodec& codec, DynamicStruct::Reader input,
              JsonValue::Builder output) const override {
    kj::Vector<FlattenedField> flattenedFields;
    gatherForEncode(codec, input, nullptr, nullptr, flattenedFields);

    auto outs = output.initObject(flattenedFields.size());
    for (auto i: kj::indices(flattenedFields)) {
      auto& in = flattenedFields[i];
      auto out = outs[i];
      out.setName(in.name);
      codec.encode(in.value, in.type, out.initValue());
    }
  }

  void decode(const JsonCodec& codec, JsonValue::Reader input,
              DynamicStruct::Builder output) const override {
    KJ_REQUIRE(input.isObject());
    std::set<const void*> unionsSeen;
    kj::Vector<JsonValue::Field::Reader> retries;
    for (auto field: input.getObject()) {
      if (!decodeField(codec, field.getName(), field.getValue(), output, unionsSeen)) {
        retries.add(field);
      }
    }
    while (!retries.empty()) {
      auto retriesCopy = kj::mv(retries);
      KJ_ASSERT(retries.empty());
      for (auto field: retriesCopy) {
        if (!decodeField(codec, field.getName(), field.getValue(), output, unionsSeen)) {
          retries.add(field);
        }
      }
      if (retries.size() == retriesCopy.size()) {
        // We made no progress in this iteration. Give up on the remaining fields.
        break;
      }
    }
  }

private:
  struct FieldInfo {
    kj::StringPtr name;
    kj::Maybe<const AnnotatedHandler&> flattenHandler;
    kj::StringPtr prefix;
  };

  kj::Array<FieldInfo> fields;
  // Maps field index -> info about the field

  struct FieldNameInfo {
    enum {
      NORMAL,
      // This is a normal field with the given `index`.

      FLATTENED,
      // This is a field of a flattened inner struct or group (that is not in a union). `index`
      // is the field index of the particular struct/group field.

      UNION_TAG,
      // The parent struct is a flattened union, and this field is the discriminant tag. It is a
      // string field whose name determines the union type. `index` is not used.

      FLATTENED_FROM_UNION
      // The parent struct is a flattened union, and some of the union's members are flattened
      // structs or groups, and this field is possibly a member of one or more of them. `index`
      // is not used, because it's possible that the same field name appears in multiple variants.
      // Intsead, the parser must find the union tag, and then can descend and attempt to parse
      // the field in the context of whichever variant is selected.
    } type;

    uint index;
    // For `NORMAL` and `FLATTENED`, the index of the field in schema.getFields().

    uint prefixLength;
    kj::String ownName;
  };

  std::map<kj::StringPtr, FieldNameInfo> fieldsByName;
  // Maps JSON names to info needed to parse them.

  std::map<kj::StringPtr, StructSchema::Field> unionTagValues;
  // If the parent struct is a flattened union, it has a tag field which is a string with one of
  // these values. The map maps to the union member to set.

  kj::Maybe<kj::StringPtr> unionTagName;
  // If the parent struct is a flattened union, the name of the "tag" field.

  uint discriminantOffset;
  // Shortcut for schema.getProto().getStruct().getDiscriminantOffset(), used in a hack to identify
  // which unions have been seen.

  struct FlattenedField {
    kj::String ownName;
    kj::StringPtr name;
    Type type;
    DynamicValue::Reader value;

    FlattenedField(kj::StringPtr prefix, kj::StringPtr name, Type type, DynamicValue::Reader value)
        : ownName(prefix.size() > 0 ? kj::str(prefix, name) : nullptr),
          name(prefix.size() > 0 ? ownName : name),
          type(type), value(value) {}
  };

  void gatherForEncode(const JsonCodec& codec, DynamicValue::Reader input,
                       kj::StringPtr prefix, kj::StringPtr morePrefix,
                       kj::Vector<FlattenedField>& flattenedFields) const {
    kj::String ownPrefix;
    if (morePrefix.size() > 0) {
      if (prefix.size() > 0) {
        ownPrefix = kj::str(prefix, morePrefix);
        prefix = ownPrefix;
      } else {
        prefix = morePrefix;
      }
    }

    auto reader = input.as<DynamicStruct>();
    auto schema = reader.getSchema();
    for (auto field: schema.getNonUnionFields()) {
      auto& info = fields[field.getIndex()];
      if (!reader.has(field, codec.impl->hasMode)) {
        // skip
      } else KJ_IF_MAYBE(handler, info.flattenHandler) {
        handler->gatherForEncode(codec, reader.get(field), prefix, info.prefix, flattenedFields);
      } else {
        flattenedFields.add(FlattenedField {
            prefix, info.name, field.getType(), reader.get(field) });
      }
    }

    KJ_IF_MAYBE(which, reader.which()) {
      auto& info = fields[which->getIndex()];
      KJ_IF_MAYBE(tag, unionTagName) {
        flattenedFields.add(FlattenedField {
            prefix, *tag, schema::Type::TEXT, Text::Reader(info.name) });
      }

      KJ_IF_MAYBE(handler, info.flattenHandler) {
        handler->gatherForEncode(codec, reader.get(*which), prefix, info.prefix, flattenedFields);
      } else {
        flattenedFields.add(FlattenedField {
            prefix, info.name, which->getType(), reader.get(*which) });
      }
    }
  }

  bool decodeField(const JsonCodec& codec, kj::StringPtr name, JsonValue::Reader value,
                   DynamicStruct::Builder output, std::set<const void*>& unionsSeen) const {
    auto iter = fieldsByName.find(name);
    if (iter == fieldsByName.end()) {
      // Ignore undefined field.
      return true;
    }

    auto& info = iter->second;
    switch (info.type) {
      case FieldNameInfo::NORMAL: {
        auto field = output.getSchema().getFields()[info.index];
        output.adopt(field, codec.decode(value, field.getType(),
            Orphanage::getForMessageContaining(output)));
        return true;
      }
      case FieldNameInfo::FLATTENED:
        return KJ_ASSERT_NONNULL(fields[info.index].flattenHandler)
            .decodeField(codec, name.slice(info.prefixLength), value,
                output.get(output.getSchema().getFields()[info.index]).as<DynamicStruct>(),
                unionsSeen);
      case FieldNameInfo::UNION_TAG: {
        KJ_REQUIRE(value.isString(), "Expected string value.");

        // Mark that we've seen a union tag for this struct.
        const void* ptr = getUnionInstanceIdentifier(output);
        KJ_REQUIRE(unionsSeen.insert(ptr).second, "Duplicate field name.");

        auto iter = unionTagValues.find(value.getString());
        if (iter != unionTagValues.end()) {
          output.init(iter->second);
        }
        return true;
      }
      case FieldNameInfo::FLATTENED_FROM_UNION: {
        const void* ptr = getUnionInstanceIdentifier(output);
        if (unionsSeen.count(ptr) == 0) {
          // We haven't seen the union tag yet, so we can't parse this field yet. Try again later.
          return false;
        }

        auto variant = KJ_ASSERT_NONNULL(output.which());
        return KJ_ASSERT_NONNULL(fields[info.index].flattenHandler)
            .decodeField(codec, name.slice(info.prefixLength), value,
                output.get(variant).as<DynamicStruct>(), unionsSeen);
      }
    }
  }

  const void* getUnionInstanceIdentifier(DynamicStruct::Builder obj) const {
    // Gets a value uniquely identifying an instance of a union.
    // HACK: We return a poniter to the union's discriminant within the underlying buffer.
    return reinterpret_cast<const uint16_t*>(
        AnyStruct::Reader(obj.asReader()).getDataSection().begin()) + discriminantOffset;
  }
};

class JsonCodec::AnnotatedEnumHandler final: public JsonCodec::Handler<DynamicEnum> {
public:
  AnnotatedEnumHandler(EnumSchema schema): schema(schema) {
    auto enumerants = schema.getEnumerants();
    auto builder = kj::heapArrayBuilder<kj::StringPtr>(enumerants.size());

    for (auto e: enumerants) {
      auto proto = e.getProto();
      kj::StringPtr name = proto.getName();

      for (auto anno: proto.getAnnotations()) {
        switch (anno.getId()) {
          case JSON_NAME_ANNOTATION_ID:
            name = anno.getValue().getText();
            break;
        }
      }

      builder.add(name);
      nameToValue.insert(std::make_pair(name, e.getIndex()));
    }

    valueToName = builder.finish();
  }

  void encode(const JsonCodec& codec, DynamicEnum input, JsonValue::Builder output) const override {
    KJ_IF_MAYBE(e, input.getEnumerant()) {
      KJ_ASSERT(e->getIndex() < valueToName.size());
      output.setString(valueToName[e->getIndex()]);
    } else {
      output.setNumber(input.getRaw());
    }
  }

  DynamicEnum decode(const JsonCodec& codec, JsonValue::Reader input) const override {
    if (input.isNumber()) {
      return DynamicEnum(schema, static_cast<uint16_t>(input.getNumber()));
    } else {
      auto iter = nameToValue.find(input.getString());
      KJ_REQUIRE(iter != nameToValue.end(), "invalid enum value", input.getString());
      return DynamicEnum(schema.getEnumerants()[iter->second]);
    }
  }

private:
  EnumSchema schema;
  kj::Array<kj::StringPtr> valueToName;
  std::map<kj::StringPtr, uint16_t> nameToValue;
};

class JsonCodec::JsonValueHandler final: public JsonCodec::Handler<DynamicStruct> {
public:
  void encode(const JsonCodec& codec, DynamicStruct::Reader input,
              JsonValue::Builder output) const override {
    rawCopy(input, kj::mv(output));
  }

  void decode(const JsonCodec& codec, JsonValue::Reader input,
              DynamicStruct::Builder output) const override {
    rawCopy(input, kj::mv(output));
  }

private:
  EnumSchema schema;
  kj::Array<kj::StringPtr> valueToName;
  std::map<kj::StringPtr, uint16_t> nameToValue;

  void rawCopy(AnyStruct::Reader input, AnyStruct::Builder output) const {
    // HACK: Manually copy using AnyStruct, so that if JsonValue's definition changes, this code
    //   doesn't need to be updated. However, note that if JsonValue ever adds new fields that
    //   change its size, and the input struct is a newer version than the output, we may lose
    //   the new fields. Technically the "correct" thing to do would be to allocate the output
    //   struct to be exactly the same size as the input, but JsonCodec's Handler interface is
    //   not designed to allow that -- it passes in an already-allocated builder. Oops.
    auto dataIn = input.getDataSection();
    auto dataOut = output.getDataSection();
    memcpy(dataOut.begin(), dataIn.begin(), kj::min(dataOut.size(), dataIn.size()));

    auto ptrIn = input.getPointerSection();
    auto ptrOut = output.getPointerSection();
    for (auto i: kj::zeroTo(kj::min(ptrIn.size(), ptrOut.size()))) {
      ptrOut[i].set(ptrIn[i]);
    }
  }
};

JsonCodec::AnnotatedHandler& JsonCodec::loadAnnotatedHandler(
      StructSchema schema, kj::Maybe<kj::StringPtr> discriminator,
      kj::Vector<Schema>& dependencies) {
  auto insertResult = impl->annotatedHandlers.insert(std::make_pair(schema, nullptr));
  if (insertResult.second) {
    // Not seen before.
    auto newHandler = kj::heap<AnnotatedHandler>(*this, schema, discriminator, dependencies);
    auto& result = *newHandler;
    insertResult.first->second = kj::mv(newHandler);
    addTypeHandler(schema, result);
    return result;
  } else KJ_IF_MAYBE(handler, insertResult.first->second) {
    return **handler;
  } else {
    KJ_FAIL_REQUIRE("cyclic JSON flattening detected", schema.getProto().getDisplayName());
  }
}

void JsonCodec::handleByAnnotation(Schema schema) {
  switch (schema.getProto().which()) {
    case schema::Node::STRUCT: {
      if (schema.getProto().getId() == capnp::typeId<JsonValue>()) {
        // Special handler for JsonValue.
        static JsonValueHandler GLOBAL_HANDLER;
        addTypeHandler(schema.asStruct(), GLOBAL_HANDLER);
      } else {
        kj::Vector<Schema> dependencies;
        loadAnnotatedHandler(schema.asStruct(), nullptr, dependencies);
        for (auto dep: dependencies) {
          handleByAnnotation(dep);
        }
      }
      break;
    }
    case schema::Node::ENUM: {
      auto enumSchema = schema.asEnum();
      auto& slot = impl->annotatedEnumHandlers[enumSchema];
      if (slot.get() == nullptr) {
        slot = kj::heap<AnnotatedEnumHandler>(enumSchema);
        addTypeHandler(enumSchema, *slot);
      }
      break;
    }
    default:
      break;
  }
}

} // namespace capnp
