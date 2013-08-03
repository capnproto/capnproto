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

// This program is a code generator plugin for capnpc which writes the schema back to stdout in
// roughly capnpc format.

#include <capnp/schema.capnp.h>
#include "../serialize.h"
#include <kj/debug.h>
#include <kj/io.h>
#include "../schema-loader.h"
#include "../dynamic.h"
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace capnp {
namespace {

class TextBlob {
public:
  TextBlob() = default;
  template <typename... Params>
  explicit TextBlob(Params&&... params);
  TextBlob(kj::Array<TextBlob>&& params);

  void writeTo(kj::OutputStream& out) const;

private:
  kj::Array<char> text;
  struct Branch;
  kj::Array<Branch> branches;

  void allocate(size_t textSize, size_t branchCount);
  template <typename First, typename... Rest>
  void allocate(size_t textSize, size_t branchCount, const First& first, Rest&&... rest);
  template <typename... Rest>
  void allocate(size_t textSize, size_t branchCount, const TextBlob& first, Rest&&... rest);

  void fill(char* textPos, Branch* branchesPos);
  template <typename First, typename... Rest>
  void fill(char* textPos, Branch* branchesPos, First&& first, Rest&&... rest);
  template <typename... Rest>
  void fill(char* textPos, Branch* branchesPos, TextBlob&& first, Rest&&... rest);

  template <typename T>
  auto toContainer(T&& t) -> decltype(kj::toCharSequence(kj::fwd<T>(t))) {
    return kj::toCharSequence(kj::fwd<T>(t));
  }
  TextBlob&& toContainer(TextBlob&& t) {
    return kj::mv(t);
  }
  TextBlob& toContainer(TextBlob& t) {
    return t;
  }
  const TextBlob& toContainer(const TextBlob& t) {
    return t;
  }

  template <typename... Params>
  void init(Params&&... params);
};

struct TextBlob::Branch {
  char* pos;
  TextBlob content;
};

template <typename... Params>
TextBlob::TextBlob(Params&&... params) {
  init(toContainer(kj::fwd<Params>(params))...);
}

TextBlob::TextBlob(kj::Array<TextBlob>&& params) {
  branches = kj::heapArray<Branch>(params.size());
  for (size_t i = 0; i < params.size(); i++) {
    branches[i].pos = nullptr;
    branches[i].content = kj::mv(params[i]);
  }
}

void TextBlob::writeTo(kj::OutputStream& out) const {
  const char* pos = text.begin();
  for (auto& branch: branches) {
    out.write(pos, branch.pos - pos);
    pos = branch.pos;
    branch.content.writeTo(out);
  }
  out.write(pos, text.end() - pos);
}

void TextBlob::allocate(size_t textSize, size_t branchCount) {
  text = kj::heapArray<char>(textSize);
  branches = kj::heapArray<Branch>(branchCount);
}

template <typename First, typename... Rest>
void TextBlob::allocate(size_t textSize, size_t branchCount, const First& first, Rest&&... rest) {
  allocate(textSize + first.size(), branchCount, kj::fwd<Rest>(rest)...);
}

template <typename... Rest>
void TextBlob::allocate(size_t textSize, size_t branchCount,
                        const TextBlob& first, Rest&&... rest) {
  allocate(textSize, branchCount + 1, kj::fwd<Rest>(rest)...);
}

void TextBlob::fill(char* textPos, Branch* branchesPos) {
  KJ_ASSERT(textPos == text.end(), textPos - text.end());
  KJ_ASSERT(branchesPos == branches.end(), branchesPos - branches.end());
}

template <typename First, typename... Rest>
void TextBlob::fill(char* textPos, Branch* branchesPos, First&& first, Rest&&... rest) {
  textPos = kj::_::fill(textPos, kj::fwd<First>(first));
  fill(textPos, branchesPos, kj::fwd<Rest>(rest)...);
}

template <typename... Rest>
void TextBlob::fill(char* textPos, Branch* branchesPos, TextBlob&& first, Rest&&... rest) {
  branchesPos->pos = textPos;
  branchesPos->content = kj::mv(first);
  ++branchesPos;
  fill(textPos, branchesPos, kj::fwd<Rest>(rest)...);
}

template <typename... Params>
void TextBlob::init(Params&&... params) {
  allocate(0, 0, params...);
  fill(text.begin(), branches.begin(), kj::fwd<Params>(params)...);
}

template <typename... Params>
TextBlob text(Params&&... params) {
  return TextBlob(kj::fwd<Params>(params)...);
}

template <typename List, typename Func>
TextBlob forText(List&& list, Func&& func) {
  kj::Array<TextBlob> items = kj::heapArray<TextBlob>(list.size());
  for (size_t i = 0; i < list.size(); i++) {
    items[i] = func(list[i]);
  }
  return TextBlob(kj::mv(items));
}

template <typename T>
struct ForTextHack {
  T list;
  ForTextHack(T list): list(kj::fwd<T>(list)) {}
  template <typename Func>
  TextBlob operator*(Func&& func) {
    return forText(list, func);
  }
};

#define FOR_EACH(list, name) ForTextHack<decltype(list)>(list) * \
    [&](decltype((list)[0]) name) -> TextBlob

struct Indent {
  uint amount;
  Indent() = default;
  inline Indent(int amount): amount(amount) {}

  Indent next() {
    return Indent(amount + 2);
  }

  struct Iterator {
    uint i;
    Iterator() = default;
    inline Iterator(uint i): i(i) {}
    inline char operator*() const { return ' '; }
    inline Iterator& operator++() { ++i; return *this; }
    inline Iterator operator++(int) { Iterator result = *this; ++i; return result; }
    inline bool operator==(const Iterator& other) const { return i == other.i; }
    inline bool operator!=(const Iterator& other) const { return i != other.i; }
  };

  inline size_t size() const { return amount; }

  inline Iterator begin() const { return Iterator(0); }
  inline Iterator end() const { return Iterator(amount); }
};

inline Indent KJ_STRINGIFY(const Indent& indent) {
  return indent;
}

// =======================================================================================

SchemaLoader schemaLoader;

Text::Reader getUnqualifiedName(Schema schema) {
  auto proto = schema.getProto();
  auto parent = schemaLoader.get(proto.getScopeId());
  for (auto nested: parent.getProto().getNestedNodes()) {
    if (nested.getId() == proto.getId()) {
      return nested.getName();
    }
  }
  KJ_FAIL_REQUIRE("A schema Node's supposed scope did not contain the node as a NestedNode.");
  return "(?)";
}

TextBlob nodeName(Schema target, Schema scope) {
  std::vector<Schema> targetParents;
  std::vector<Schema> scopeParts;

  {
    Schema parent = target;
    while (parent.getProto().getScopeId() != 0) {
      parent = schemaLoader.get(parent.getProto().getScopeId());
      targetParents.push_back(parent);
    }
  }

  {
    Schema parent = scope;
    scopeParts.push_back(parent);
    while (parent.getProto().getScopeId() != 0) {
      parent = schemaLoader.get(parent.getProto().getScopeId());
      scopeParts.push_back(parent);
    }
  }

  // Remove common scope.
  while (!scopeParts.empty() && !targetParents.empty() &&
         scopeParts.back() == targetParents.back()) {
    scopeParts.pop_back();
    targetParents.pop_back();
  }

  // TODO(someday):  This is broken in that we aren't checking for shadowing.

  TextBlob path = text();
  while (!targetParents.empty()) {
    auto part = targetParents.back();
    auto proto = part.getProto();
    if (proto.getScopeId() == 0) {
      path = text(kj::mv(path), "import \"", proto.getDisplayName(), "\".");
    } else {
      path = text(kj::mv(path), getUnqualifiedName(part), ".");
    }
    targetParents.pop_back();
  }

  return text(kj::mv(path), getUnqualifiedName(target));
}

TextBlob genType(schema::Type::Reader type, Schema scope) {
  auto body = type.getBody();
  switch (body.which()) {
    case schema::Type::Body::VOID_TYPE: return text("Void");
    case schema::Type::Body::BOOL_TYPE: return text("Bool");
    case schema::Type::Body::INT8_TYPE: return text("Int8");
    case schema::Type::Body::INT16_TYPE: return text("Int16");
    case schema::Type::Body::INT32_TYPE: return text("Int32");
    case schema::Type::Body::INT64_TYPE: return text("Int64");
    case schema::Type::Body::UINT8_TYPE: return text("UInt8");
    case schema::Type::Body::UINT16_TYPE: return text("UInt16");
    case schema::Type::Body::UINT32_TYPE: return text("UInt32");
    case schema::Type::Body::UINT64_TYPE: return text("UInt64");
    case schema::Type::Body::FLOAT32_TYPE: return text("Float32");
    case schema::Type::Body::FLOAT64_TYPE: return text("Float64");
    case schema::Type::Body::TEXT_TYPE: return text("Text");
    case schema::Type::Body::DATA_TYPE: return text("Data");
    case schema::Type::Body::LIST_TYPE:
      return text("List(", genType(body.getListType(), scope), ")");
    case schema::Type::Body::ENUM_TYPE:
      return nodeName(scope.getDependency(body.getEnumType()), scope);
    case schema::Type::Body::STRUCT_TYPE:
      return nodeName(scope.getDependency(body.getStructType()), scope);
    case schema::Type::Body::INTERFACE_TYPE:
      return nodeName(scope.getDependency(body.getInterfaceType()), scope);
    case schema::Type::Body::OBJECT_TYPE: return text("Object");
  }
  return text();
}

int typeSizeBits(schema::Type::Reader type) {
  switch (type.getBody().which()) {
    case schema::Type::Body::VOID_TYPE: return 0;
    case schema::Type::Body::BOOL_TYPE: return 1;
    case schema::Type::Body::INT8_TYPE: return 8;
    case schema::Type::Body::INT16_TYPE: return 16;
    case schema::Type::Body::INT32_TYPE: return 32;
    case schema::Type::Body::INT64_TYPE: return 64;
    case schema::Type::Body::UINT8_TYPE: return 8;
    case schema::Type::Body::UINT16_TYPE: return 16;
    case schema::Type::Body::UINT32_TYPE: return 32;
    case schema::Type::Body::UINT64_TYPE: return 64;
    case schema::Type::Body::FLOAT32_TYPE: return 32;
    case schema::Type::Body::FLOAT64_TYPE: return 64;
    case schema::Type::Body::TEXT_TYPE: return -1;
    case schema::Type::Body::DATA_TYPE: return -1;
    case schema::Type::Body::LIST_TYPE: return -1;
    case schema::Type::Body::ENUM_TYPE: return 16;
    case schema::Type::Body::STRUCT_TYPE: return -1;
    case schema::Type::Body::INTERFACE_TYPE: return -1;
    case schema::Type::Body::OBJECT_TYPE: return -1;
  }
  return 0;
}

bool isEmptyValue(schema::Value::Reader value) {
  auto body = value.getBody();
  switch (body.which()) {
    case schema::Value::Body::VOID_VALUE: return true;
    case schema::Value::Body::BOOL_VALUE: return body.getBoolValue() == false;
    case schema::Value::Body::INT8_VALUE: return body.getInt8Value() == 0;
    case schema::Value::Body::INT16_VALUE: return body.getInt16Value() == 0;
    case schema::Value::Body::INT32_VALUE: return body.getInt32Value() == 0;
    case schema::Value::Body::INT64_VALUE: return body.getInt64Value() == 0;
    case schema::Value::Body::UINT8_VALUE: return body.getUint8Value() == 0;
    case schema::Value::Body::UINT16_VALUE: return body.getUint16Value() == 0;
    case schema::Value::Body::UINT32_VALUE: return body.getUint32Value() == 0;
    case schema::Value::Body::UINT64_VALUE: return body.getUint64Value() == 0;
    case schema::Value::Body::FLOAT32_VALUE: return body.getFloat32Value() == 0;
    case schema::Value::Body::FLOAT64_VALUE: return body.getFloat64Value() == 0;
    case schema::Value::Body::TEXT_VALUE: return !body.hasTextValue();
    case schema::Value::Body::DATA_VALUE: return !body.hasDataValue();
    case schema::Value::Body::LIST_VALUE: return !body.hasListValue();
    case schema::Value::Body::ENUM_VALUE: return body.getEnumValue() == 0;
    case schema::Value::Body::STRUCT_VALUE: return !body.hasStructValue();
    case schema::Value::Body::INTERFACE_VALUE: return true;
    case schema::Value::Body::OBJECT_VALUE: return true;
  }
  return true;
}

TextBlob genValue(schema::Type::Reader type, schema::Value::Reader value, Schema scope) {
  auto body = value.getBody();
  switch (body.which()) {
    case schema::Value::Body::VOID_VALUE: return text("void");
    case schema::Value::Body::BOOL_VALUE: return text(body.getBoolValue() ? "true" : "false");
    case schema::Value::Body::INT8_VALUE: return text((int)body.getInt8Value());
    case schema::Value::Body::INT16_VALUE: return text(body.getInt16Value());
    case schema::Value::Body::INT32_VALUE: return text(body.getInt32Value());
    case schema::Value::Body::INT64_VALUE: return text(body.getInt64Value());
    case schema::Value::Body::UINT8_VALUE: return text((uint)body.getUint8Value());
    case schema::Value::Body::UINT16_VALUE: return text(body.getUint16Value());
    case schema::Value::Body::UINT32_VALUE: return text(body.getUint32Value());
    case schema::Value::Body::UINT64_VALUE: return text(body.getUint64Value());
    case schema::Value::Body::FLOAT32_VALUE: return text(body.getFloat32Value());
    case schema::Value::Body::FLOAT64_VALUE: return text(body.getFloat64Value());
    case schema::Value::Body::TEXT_VALUE: return text(DynamicValue::Reader(body.getTextValue()));
    case schema::Value::Body::DATA_VALUE: return text(DynamicValue::Reader(body.getDataValue()));
    case schema::Value::Body::LIST_VALUE: {
      KJ_REQUIRE(type.getBody().which() == schema::Type::Body::LIST_TYPE, "type/value mismatch");
      auto value = body.getListValue<DynamicList>(
          ListSchema::of(type.getBody().getListType(), scope));
      return text(value);
    }
    case schema::Value::Body::ENUM_VALUE: {
      KJ_REQUIRE(type.getBody().which() == schema::Type::Body::ENUM_TYPE, "type/value mismatch");
      auto enumNode = scope.getDependency(type.getBody().getEnumType()).asEnum().getProto();
      auto enumType = enumNode.getBody().getEnumNode();
      auto enumerants = enumType.getEnumerants();
      KJ_REQUIRE(body.getEnumValue() < enumerants.size(),
              "Enum value out-of-range.", body.getEnumValue(), enumNode.getDisplayName());
      return text(enumerants[body.getEnumValue()].getName());
    }
    case schema::Value::Body::STRUCT_VALUE: {
      KJ_REQUIRE(type.getBody().which() == schema::Type::Body::STRUCT_TYPE, "type/value mismatch");
      auto value = body.getStructValue<DynamicStruct>(
          scope.getDependency(type.getBody().getStructType()).asStruct());
      return text(value);
    }
    case schema::Value::Body::INTERFACE_VALUE: {
      return text("");
    }
    case schema::Value::Body::OBJECT_VALUE: {
      return text("");
    }
  }
  return text("");
}

TextBlob genAnnotation(schema::Annotation::Reader annotation,
                       Schema scope,
                       const char* prefix = " ", const char* suffix = "") {
  auto decl = schemaLoader.get(annotation.getId());
  auto body = decl.getProto().getBody();
  KJ_REQUIRE(body.which() == schema::Node::Body::ANNOTATION_NODE);
  auto annDecl = body.getAnnotationNode();

  return text(prefix, "$", nodeName(decl, scope), "(",
              genValue(annDecl.getType(), annotation.getValue(), scope), ")", suffix);
}

TextBlob genAnnotations(List<schema::Annotation>::Reader list, Schema scope) {
  return FOR_EACH(list, ann) { return genAnnotation(ann, scope); };
}
TextBlob genAnnotations(Schema schema) {
  auto proto = schema.getProto();
  return genAnnotations(proto.getAnnotations(), schemaLoader.get(proto.getScopeId()));
}

const char* elementSizeName(schema::ElementSize size) {
  switch (size) {
    case schema::ElementSize::EMPTY: return "void";
    case schema::ElementSize::BIT: return "1-bit";
    case schema::ElementSize::BYTE: return "8-bit";
    case schema::ElementSize::TWO_BYTES: return "16-bit";
    case schema::ElementSize::FOUR_BYTES: return "32-bit";
    case schema::ElementSize::EIGHT_BYTES: return "64-bit";
    case schema::ElementSize::POINTER: return "pointer";
    case schema::ElementSize::INLINE_COMPOSITE: return "inline composite";
  }
  return "";
}

TextBlob genStructMember(schema::StructNode::Member::Reader member,
                         Schema scope, Indent indent, int unionTag = -1) {
  switch (member.getBody().which()) {
    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getBody().getFieldMember();
      int size = typeSizeBits(field.getType());
      return text(indent, member.getName(), " @", member.getOrdinal(),
                  " :", genType(field.getType(), scope),
                  isEmptyValue(field.getDefaultValue()) ? text("") :
                      text(" = ", genValue(field.getType(), field.getDefaultValue(), scope)),
                  genAnnotations(member.getAnnotations(), scope),
                  ";  # ", size == -1 ? text("ptr[", field.getOffset(), "]")
                                      : text("bits[", field.getOffset() * size, ", ",
                                                     (field.getOffset() + 1) * size, ")"),
                  unionTag != -1 ? text(", union tag = ", unionTag) : text(),
                  "\n");
    }
    case schema::StructNode::Member::Body::UNION_MEMBER: {
      auto un = member.getBody().getUnionMember();
      int i = 0;
      return text(indent, member.getName(), " @", member.getOrdinal(),
                  " union", genAnnotations(member.getAnnotations(), scope),
                  " {  # tag bits[", un.getDiscriminantOffset() * 16, ", ",
                  un.getDiscriminantOffset() * 16 + 16, ")\n",
                  FOR_EACH(un.getMembers(), member) {
                    return genStructMember(member, scope, indent.next(), i++);
                  },
                  indent, "}\n");
    }
  }
  return text();
}

TextBlob genNestedDecls(Schema schema, Indent indent);

TextBlob genDecl(Schema schema, Text::Reader name, uint64_t scopeId, Indent indent) {
  auto proto = schema.getProto();
  if (proto.getScopeId() != scopeId) {
    // This appears to be an alias for something declared elsewhere.
    KJ_FAIL_REQUIRE("Aliases not implemented.");
  }

  switch (proto.getBody().which()) {
    case schema::Node::Body::FILE_NODE:
      KJ_FAIL_REQUIRE("Encountered nested file node.");
      break;
    case schema::Node::Body::STRUCT_NODE: {
      auto body = proto.getBody().getStructNode();
      return text(
          indent, "struct ", name, " @0x", kj::hex(proto.getId()), genAnnotations(schema), " {  # ",
          body.getDataSectionWordSize() * 8, " bytes, ",
          body.getPointerSectionSize(), " ptrs",
          body.getPreferredListEncoding() == schema::ElementSize::INLINE_COMPOSITE
              ? text()
              : text(", packed as ", elementSizeName(body.getPreferredListEncoding())),
          "\n",
          FOR_EACH(body.getMembers(), member) {
            return genStructMember(member, schema, indent.next());
          },
          genNestedDecls(schema, indent.next()),
          indent, "}\n");
    }
    case schema::Node::Body::ENUM_NODE: {
      auto body = proto.getBody().getEnumNode();
      uint i = 0;
      return text(
          indent, "enum ", name, " @0x", kj::hex(proto.getId()), genAnnotations(schema), " {\n",
          FOR_EACH(body.getEnumerants(), enumerant) {
            return text(indent.next(), enumerant.getName(), " @", i++,
                        genAnnotations(enumerant.getAnnotations(), schema), ";\n");
          },
          genNestedDecls(schema, indent.next()),
          indent, "}\n");
    }
    case schema::Node::Body::INTERFACE_NODE: {
      auto body = proto.getBody().getInterfaceNode();
      uint i = 0;
      return text(
          indent, "interface ", name, " @0x", kj::hex(proto.getId()),
          genAnnotations(schema), " {\n",
          FOR_EACH(body.getMethods(), method) {
            int j = 0;
            return text(
                indent.next(), method.getName(), " @", i++, "(",
                FOR_EACH(method.getParams(), param) {
                  bool hasDefault = j >= method.getRequiredParamCount() ||
                      !isEmptyValue(param.getDefaultValue());
                  return text(
                      j++ > 0 ? ", " : "",
                      param.getName(), ": ", genType(param.getType(), schema),
                      hasDefault
                          ? text(" = ", genValue(param.getType(), param.getDefaultValue(), schema))
                          : text(),
                      genAnnotations(param.getAnnotations(), schema));
                },
                ") :", genType(method.getReturnType(), schema),
                genAnnotations(method.getAnnotations(), schema), ";\n");
          },
          genNestedDecls(schema, indent.next()),
          indent, "}\n");
    }
    case schema::Node::Body::CONST_NODE: {
      auto body = proto.getBody().getConstNode();
      return text(
          indent, "const ", name, " @0x", kj::hex(proto.getId()), " :",
          genType(body.getType(), schema), " = ",
          genValue(body.getType(), body.getValue(), schema), ";\n");
    }
    case schema::Node::Body::ANNOTATION_NODE: {
      auto body = proto.getBody().getAnnotationNode();
      kj::CappedArray<const char*, 11> targets;
      uint i = 0;
      if (body.getTargetsFile()) targets[i++] = "file";
      if (body.getTargetsConst()) targets[i++] = "const";
      if (body.getTargetsEnum()) targets[i++] = "enum";
      if (body.getTargetsEnumerant()) targets[i++] = "enumerant";
      if (body.getTargetsStruct()) targets[i++] = "struct";
      if (body.getTargetsField()) targets[i++] = "field";
      if (body.getTargetsUnion()) targets[i++] = "union";
      if (body.getTargetsInterface()) targets[i++] = "interface";
      if (body.getTargetsMethod()) targets[i++] = "method";
      if (body.getTargetsParam()) targets[i++] = "param";
      if (body.getTargetsAnnotation()) targets[i++] = "annotation";
      if (i == targets.size()) {
        targets[0] = "*";
        targets.setSize(1);
      } else {
        targets.setSize(i);
      }
      return text(
          indent, "annotation ", name, " @0x", kj::hex(proto.getId()),
          " (", strArray(targets, ", "), ") :",
          genType(body.getType(), schema), genAnnotations(schema), ";\n");
    }
  }

  return text();
}

TextBlob genNestedDecls(Schema schema, Indent indent) {
  uint64_t id = schema.getProto().getId();
  return FOR_EACH(schema.getProto().getNestedNodes(), nested) {
    return genDecl(schemaLoader.get(nested.getId()), nested.getName(), id, indent);
  };
}

TextBlob genFile(Schema file) {
  auto proto = file.getProto();
  auto body = proto.getBody();
  KJ_REQUIRE(body.which() == schema::Node::Body::FILE_NODE, "Expected a file node.",
          (uint)body.which());

  return text(
    "# ", proto.getDisplayName(), "\n",
    "@0x", kj::hex(proto.getId()), ";\n",
    FOR_EACH(proto.getAnnotations(), ann) { return genAnnotation(ann, file, "", ";\n"); },
    genNestedDecls(file, Indent(0)));
}

int main(int argc, char* argv[]) {
  ReaderOptions options;
  options.traversalLimitInWords = 1 << 30;  // Don't limit.
  StreamFdMessageReader reader(STDIN_FILENO, options);
  auto request = reader.getRoot<schema::CodeGeneratorRequest>();

  for (auto node: request.getNodes()) {
    schemaLoader.load(node);
  }

  kj::FdOutputStream rawOut(STDOUT_FILENO);
  kj::BufferedOutputStreamWrapper out(rawOut);

  for (auto fileId: request.getRequestedFiles()) {
    genFile(schemaLoader.get(fileId)).writeTo(out);
  }

  return 0;
}

}  // namespace
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::main(argc, argv);
}
