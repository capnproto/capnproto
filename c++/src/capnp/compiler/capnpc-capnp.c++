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

// This program is a code generator plugin for `capnp compile` which writes the schema back to
// stdout in roughly capnpc format.

#include <capnp/schema.capnp.h>
#include "../serialize.h"
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string-tree.h>
#include <kj/vector.h>
#include "../schema-loader.h"
#include "../dynamic.h"
#include <unistd.h>
#include <unordered_map>
#include <kj/main.h>

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace {

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

class CapnpcCapnpMain {
public:
  CapnpcCapnpMain(kj::ProcessContext& context): context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Cap'n Proto loopback plugin version " VERSION,
          "This is a Cap'n Proto compiler plugin which \"de-compiles\" the schema back into "
          "Cap'n Proto schema language format, with comments showing the offsets chosen by the "
          "compiler.  This is meant to be run using the Cap'n Proto compiler, e.g.:\n"
          "    capnp compile -ocapnp foo.capnp")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

private:
  kj::ProcessContext& context;
  SchemaLoader schemaLoader;

  Text::Reader getUnqualifiedName(Schema schema) {
    auto proto = schema.getProto();
    KJ_CONTEXT(proto.getDisplayName());
    auto parent = schemaLoader.get(proto.getScopeId());
    for (auto nested: parent.getProto().getNestedNodes()) {
      if (nested.getId() == proto.getId()) {
        return nested.getName();
      }
    }
    KJ_FAIL_REQUIRE("A schema Node's supposed scope did not contain the node as a NestedNode.");
    return "(?)";
  }

  kj::StringTree nodeName(Schema target, Schema scope) {
    kj::Vector<Schema> targetParents;
    kj::Vector<Schema> scopeParts;

    {
      Schema parent = target;
      while (parent.getProto().getScopeId() != 0) {
        parent = schemaLoader.get(parent.getProto().getScopeId());
        targetParents.add(parent);
      }
    }

    {
      Schema parent = scope;
      scopeParts.add(parent);
      while (parent.getProto().getScopeId() != 0) {
        parent = schemaLoader.get(parent.getProto().getScopeId());
        scopeParts.add(parent);
      }
    }

    // Remove common scope.
    while (!scopeParts.empty() && !targetParents.empty() &&
           scopeParts.back() == targetParents.back()) {
      scopeParts.removeLast();
      targetParents.removeLast();
    }

    // TODO(someday):  This is broken in that we aren't checking for shadowing.

    kj::StringTree path = kj::strTree();
    while (!targetParents.empty()) {
      auto part = targetParents.back();
      auto proto = part.getProto();
      if (proto.getScopeId() == 0) {
        path = kj::strTree(kj::mv(path), "import \"/", proto.getDisplayName(), "\".");
      } else {
        path = kj::strTree(kj::mv(path), getUnqualifiedName(part), ".");
      }
      targetParents.removeLast();
    }

    return kj::strTree(kj::mv(path), getUnqualifiedName(target));
  }

  kj::StringTree genType(schema::Type::Reader type, Schema scope) {
    auto body = type.getBody();
    switch (body.which()) {
      case schema::Type::Body::VOID_TYPE: return kj::strTree("Void");
      case schema::Type::Body::BOOL_TYPE: return kj::strTree("Bool");
      case schema::Type::Body::INT8_TYPE: return kj::strTree("Int8");
      case schema::Type::Body::INT16_TYPE: return kj::strTree("Int16");
      case schema::Type::Body::INT32_TYPE: return kj::strTree("Int32");
      case schema::Type::Body::INT64_TYPE: return kj::strTree("Int64");
      case schema::Type::Body::UINT8_TYPE: return kj::strTree("UInt8");
      case schema::Type::Body::UINT16_TYPE: return kj::strTree("UInt16");
      case schema::Type::Body::UINT32_TYPE: return kj::strTree("UInt32");
      case schema::Type::Body::UINT64_TYPE: return kj::strTree("UInt64");
      case schema::Type::Body::FLOAT32_TYPE: return kj::strTree("Float32");
      case schema::Type::Body::FLOAT64_TYPE: return kj::strTree("Float64");
      case schema::Type::Body::TEXT_TYPE: return kj::strTree("Text");
      case schema::Type::Body::DATA_TYPE: return kj::strTree("Data");
      case schema::Type::Body::LIST_TYPE:
        return kj::strTree("List(", genType(body.getListType(), scope), ")");
      case schema::Type::Body::ENUM_TYPE:
        return nodeName(scope.getDependency(body.getEnumType()), scope);
      case schema::Type::Body::STRUCT_TYPE:
        return nodeName(scope.getDependency(body.getStructType()), scope);
      case schema::Type::Body::INTERFACE_TYPE:
        return nodeName(scope.getDependency(body.getInterfaceType()), scope);
      case schema::Type::Body::OBJECT_TYPE: return kj::strTree("Object");
    }
    return kj::strTree();
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

  kj::StringTree genValue(schema::Type::Reader type, schema::Value::Reader value, Schema scope) {
    auto body = value.getBody();
    switch (body.which()) {
      case schema::Value::Body::VOID_VALUE: return kj::strTree("void");
      case schema::Value::Body::BOOL_VALUE:
        return kj::strTree(body.getBoolValue() ? "true" : "false");
      case schema::Value::Body::INT8_VALUE: return kj::strTree((int)body.getInt8Value());
      case schema::Value::Body::INT16_VALUE: return kj::strTree(body.getInt16Value());
      case schema::Value::Body::INT32_VALUE: return kj::strTree(body.getInt32Value());
      case schema::Value::Body::INT64_VALUE: return kj::strTree(body.getInt64Value());
      case schema::Value::Body::UINT8_VALUE: return kj::strTree((uint)body.getUint8Value());
      case schema::Value::Body::UINT16_VALUE: return kj::strTree(body.getUint16Value());
      case schema::Value::Body::UINT32_VALUE: return kj::strTree(body.getUint32Value());
      case schema::Value::Body::UINT64_VALUE: return kj::strTree(body.getUint64Value());
      case schema::Value::Body::FLOAT32_VALUE: return kj::strTree(body.getFloat32Value());
      case schema::Value::Body::FLOAT64_VALUE: return kj::strTree(body.getFloat64Value());
      case schema::Value::Body::TEXT_VALUE:
        return kj::strTree(DynamicValue::Reader(body.getTextValue()));
      case schema::Value::Body::DATA_VALUE:
        return kj::strTree(DynamicValue::Reader(body.getDataValue()));
      case schema::Value::Body::LIST_VALUE: {
        KJ_REQUIRE(type.getBody().which() == schema::Type::Body::LIST_TYPE, "type/value mismatch");
        auto value = body.getListValue<DynamicList>(
            ListSchema::of(type.getBody().getListType(), scope));
        return kj::strTree(value);
      }
      case schema::Value::Body::ENUM_VALUE: {
        KJ_REQUIRE(type.getBody().which() == schema::Type::Body::ENUM_TYPE, "type/value mismatch");
        auto enumNode = scope.getDependency(type.getBody().getEnumType()).asEnum().getProto();
        auto enumType = enumNode.getBody().getEnumNode();
        auto enumerants = enumType.getEnumerants();
        KJ_REQUIRE(body.getEnumValue() < enumerants.size(),
                "Enum value out-of-range.", body.getEnumValue(), enumNode.getDisplayName());
        return kj::strTree(enumerants[body.getEnumValue()].getName());
      }
      case schema::Value::Body::STRUCT_VALUE: {
        KJ_REQUIRE(type.getBody().which() == schema::Type::Body::STRUCT_TYPE,
                   "type/value mismatch");
        auto value = body.getStructValue<DynamicStruct>(
            scope.getDependency(type.getBody().getStructType()).asStruct());
        return kj::strTree(value);
      }
      case schema::Value::Body::INTERFACE_VALUE: {
        return kj::strTree("");
      }
      case schema::Value::Body::OBJECT_VALUE: {
        return kj::strTree("");
      }
    }
    return kj::strTree("");
  }

  kj::StringTree genAnnotation(schema::Annotation::Reader annotation,
                               Schema scope,
                               const char* prefix = " ", const char* suffix = "") {
    auto decl = schemaLoader.get(annotation.getId());
    auto body = decl.getProto().getBody();
    KJ_REQUIRE(body.which() == schema::Node::Body::ANNOTATION_NODE);
    auto annDecl = body.getAnnotationNode();

    return kj::strTree(prefix, "$", nodeName(decl, scope), "(",
                       genValue(annDecl.getType(), annotation.getValue(), scope), ")", suffix);
  }

  kj::StringTree genAnnotations(List<schema::Annotation>::Reader list, Schema scope) {
    return kj::strTree(KJ_MAP(list, ann) { return genAnnotation(ann, scope); });
  }
  kj::StringTree genAnnotations(Schema schema) {
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

  kj::StringTree genStructMember(schema::StructNode::Member::Reader member,
                                 Schema scope, Indent indent, int unionTag = -1) {
    switch (member.getBody().which()) {
      case schema::StructNode::Member::Body::FIELD_MEMBER: {
        auto field = member.getBody().getFieldMember();
        int size = typeSizeBits(field.getType());
        return kj::strTree(
            indent, member.getName(), " @", member.getOrdinal(),
            " :", genType(field.getType(), scope),
            isEmptyValue(field.getDefaultValue()) ? kj::strTree("") :
                kj::strTree(" = ", genValue(field.getType(), field.getDefaultValue(), scope)),
            genAnnotations(member.getAnnotations(), scope),
            ";  # ", size == -1 ? kj::strTree("ptr[", field.getOffset(), "]")
                                : kj::strTree("bits[", field.getOffset() * size, ", ",
                                              (field.getOffset() + 1) * size, ")"),
            unionTag != -1 ? kj::strTree(", union tag = ", unionTag) : kj::strTree(),
            "\n");
      }
      case schema::StructNode::Member::Body::UNION_MEMBER: {
        auto un = member.getBody().getUnionMember();
        int i = 0;
        return kj::strTree(
            indent, member.getName(), " @", member.getOrdinal(),
            " union", genAnnotations(member.getAnnotations(), scope),
            " {  # tag bits[", un.getDiscriminantOffset() * 16, ", ",
            un.getDiscriminantOffset() * 16 + 16, ")\n",
            KJ_MAP(un.getMembers(), member) {
              return genStructMember(member, scope, indent.next(), i++);
            },
            indent, "}\n");
      }
      case schema::StructNode::Member::Body::GROUP_MEMBER: {
        auto group = member.getBody().getGroupMember();
        return kj::strTree(
            indent, member.getName(),
            " group", genAnnotations(member.getAnnotations(), scope), " {",
            unionTag != -1 ? kj::strTree("  # union tag = ", unionTag) : kj::strTree(), "\n",
            KJ_MAP(group.getMembers(), member) {
              return genStructMember(member, scope, indent.next());
            },
            indent, "}\n");
      }
    }
    return kj::strTree();
  }

  kj::StringTree genDecl(Schema schema, Text::Reader name, uint64_t scopeId, Indent indent) {
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
        return kj::strTree(
            indent, "struct ", name,
            " @0x", kj::hex(proto.getId()), genAnnotations(schema), " {  # ",
            body.getDataSectionWordSize() * 8, " bytes, ",
            body.getPointerSectionSize(), " ptrs",
            body.getPreferredListEncoding() == schema::ElementSize::INLINE_COMPOSITE
                ? kj::strTree()
                : kj::strTree(", packed as ", elementSizeName(body.getPreferredListEncoding())),
            "\n",
            KJ_MAP(body.getMembers(), member) {
              return genStructMember(member, schema, indent.next());
            },
            genNestedDecls(schema, indent.next()),
            indent, "}\n");
      }
      case schema::Node::Body::ENUM_NODE: {
        auto body = proto.getBody().getEnumNode();
        uint i = 0;
        return kj::strTree(
            indent, "enum ", name, " @0x", kj::hex(proto.getId()), genAnnotations(schema), " {\n",
            KJ_MAP(body.getEnumerants(), enumerant) {
              return kj::strTree(indent.next(), enumerant.getName(), " @", i++,
                                 genAnnotations(enumerant.getAnnotations(), schema), ";\n");
            },
            genNestedDecls(schema, indent.next()),
            indent, "}\n");
      }
      case schema::Node::Body::INTERFACE_NODE: {
        auto body = proto.getBody().getInterfaceNode();
        uint i = 0;
        return kj::strTree(
            indent, "interface ", name, " @0x", kj::hex(proto.getId()),
            genAnnotations(schema), " {\n",
            KJ_MAP(body.getMethods(), method) {
              int j = 0;
              return kj::strTree(
                  indent.next(), method.getName(), " @", i++, "(",
                  KJ_MAP(method.getParams(), param) {
                    bool hasDefault = j >= method.getRequiredParamCount() ||
                        !isEmptyValue(param.getDefaultValue());
                    return kj::strTree(
                        j++ > 0 ? ", " : "",
                        param.getName(), ": ", genType(param.getType(), schema),
                        hasDefault
                            ? kj::strTree(" = ", genValue(
                                param.getType(), param.getDefaultValue(), schema))
                            : kj::strTree(),
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
        return kj::strTree(
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
        return kj::strTree(
            indent, "annotation ", name, " @0x", kj::hex(proto.getId()),
            " (", strArray(targets, ", "), ") :",
            genType(body.getType(), schema), genAnnotations(schema), ";\n");
      }
    }

    return kj::strTree();
  }

  kj::StringTree genNestedDecls(Schema schema, Indent indent) {
    uint64_t id = schema.getProto().getId();
    return kj::strTree(KJ_MAP(schema.getProto().getNestedNodes(), nested) {
      return genDecl(schemaLoader.get(nested.getId()), nested.getName(), id, indent);
    });
  }

  kj::StringTree genFile(Schema file) {
    auto proto = file.getProto();
    auto body = proto.getBody();
    KJ_REQUIRE(body.which() == schema::Node::Body::FILE_NODE, "Expected a file node.",
            (uint)body.which());

    return kj::strTree(
      "# ", proto.getDisplayName(), "\n",
      "@0x", kj::hex(proto.getId()), ";\n",
      KJ_MAP(proto.getAnnotations(), ann) { return genAnnotation(ann, file, "", ";\n"); },
      genNestedDecls(file, Indent(0)));
  }

  kj::MainBuilder::Validity run() {
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
      genFile(schemaLoader.get(fileId)).visit(
          [&](kj::ArrayPtr<const char> text) {
            out.write(text.begin(), text.size());
          });
    }

    return true;
  }
};

}  // namespace
}  // namespace capnp

KJ_MAIN(capnp::CapnpcCapnpMain);
