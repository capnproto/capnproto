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
#include <algorithm>

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
    switch (type.which()) {
      case schema::Type::VOID: return kj::strTree("Void");
      case schema::Type::BOOL: return kj::strTree("Bool");
      case schema::Type::INT8: return kj::strTree("Int8");
      case schema::Type::INT16: return kj::strTree("Int16");
      case schema::Type::INT32: return kj::strTree("Int32");
      case schema::Type::INT64: return kj::strTree("Int64");
      case schema::Type::UINT8: return kj::strTree("UInt8");
      case schema::Type::UINT16: return kj::strTree("UInt16");
      case schema::Type::UINT32: return kj::strTree("UInt32");
      case schema::Type::UINT64: return kj::strTree("UInt64");
      case schema::Type::FLOAT32: return kj::strTree("Float32");
      case schema::Type::FLOAT64: return kj::strTree("Float64");
      case schema::Type::TEXT: return kj::strTree("Text");
      case schema::Type::DATA: return kj::strTree("Data");
      case schema::Type::LIST:
        return kj::strTree("List(", genType(type.getList().getElementType(), scope), ")");
      case schema::Type::ENUM:
        return nodeName(scope.getDependency(type.getEnum().getTypeId()), scope);
      case schema::Type::STRUCT:
        return nodeName(scope.getDependency(type.getStruct().getTypeId()), scope);
      case schema::Type::INTERFACE:
        return nodeName(scope.getDependency(type.getInterface().getTypeId()), scope);
      case schema::Type::OBJECT: return kj::strTree("Object");
    }
    return kj::strTree();
  }

  int typeSizeBits(schema::Type::Reader type) {
    switch (type.which()) {
      case schema::Type::VOID: return 0;
      case schema::Type::BOOL: return 1;
      case schema::Type::INT8: return 8;
      case schema::Type::INT16: return 16;
      case schema::Type::INT32: return 32;
      case schema::Type::INT64: return 64;
      case schema::Type::UINT8: return 8;
      case schema::Type::UINT16: return 16;
      case schema::Type::UINT32: return 32;
      case schema::Type::UINT64: return 64;
      case schema::Type::FLOAT32: return 32;
      case schema::Type::FLOAT64: return 64;
      case schema::Type::TEXT: return -1;
      case schema::Type::DATA: return -1;
      case schema::Type::LIST: return -1;
      case schema::Type::ENUM: return 16;
      case schema::Type::STRUCT: return -1;
      case schema::Type::INTERFACE: return -1;
      case schema::Type::OBJECT: return -1;
    }
    return 0;
  }

  bool isEmptyValue(schema::Value::Reader value) {
    switch (value.which()) {
      case schema::Value::VOID: return true;
      case schema::Value::BOOL: return value.getBool() == false;
      case schema::Value::INT8: return value.getInt8() == 0;
      case schema::Value::INT16: return value.getInt16() == 0;
      case schema::Value::INT32: return value.getInt32() == 0;
      case schema::Value::INT64: return value.getInt64() == 0;
      case schema::Value::UINT8: return value.getUint8() == 0;
      case schema::Value::UINT16: return value.getUint16() == 0;
      case schema::Value::UINT32: return value.getUint32() == 0;
      case schema::Value::UINT64: return value.getUint64() == 0;
      case schema::Value::FLOAT32: return value.getFloat32() == 0;
      case schema::Value::FLOAT64: return value.getFloat64() == 0;
      case schema::Value::TEXT: return !value.hasText();
      case schema::Value::DATA: return !value.hasData();
      case schema::Value::LIST: return !value.hasList();
      case schema::Value::ENUM: return value.getEnum() == 0;
      case schema::Value::STRUCT: return !value.hasStruct();
      case schema::Value::INTERFACE: return true;
      case schema::Value::OBJECT: return true;
    }
    return true;
  }

  kj::StringTree genValue(schema::Type::Reader type, schema::Value::Reader value, Schema scope) {
    switch (value.which()) {
      case schema::Value::VOID: return kj::strTree("void");
      case schema::Value::BOOL:
        return kj::strTree(value.getBool() ? "true" : "false");
      case schema::Value::INT8: return kj::strTree((int)value.getInt8());
      case schema::Value::INT16: return kj::strTree(value.getInt16());
      case schema::Value::INT32: return kj::strTree(value.getInt32());
      case schema::Value::INT64: return kj::strTree(value.getInt64());
      case schema::Value::UINT8: return kj::strTree((uint)value.getUint8());
      case schema::Value::UINT16: return kj::strTree(value.getUint16());
      case schema::Value::UINT32: return kj::strTree(value.getUint32());
      case schema::Value::UINT64: return kj::strTree(value.getUint64());
      case schema::Value::FLOAT32: return kj::strTree(value.getFloat32());
      case schema::Value::FLOAT64: return kj::strTree(value.getFloat64());
      case schema::Value::TEXT:
        return kj::strTree(DynamicValue::Reader(value.getText()));
      case schema::Value::DATA:
        return kj::strTree(DynamicValue::Reader(value.getData()));
      case schema::Value::LIST: {
        KJ_REQUIRE(type.isList(), "type/value mismatch");
        auto listValue = value.getList().getAs<DynamicList>(
            ListSchema::of(type.getList().getElementType(), scope));
        return kj::strTree(listValue);
      }
      case schema::Value::ENUM: {
        KJ_REQUIRE(type.isEnum(), "type/value mismatch");
        auto enumNode = scope.getDependency(type.getEnum().getTypeId()).asEnum().getProto();
        auto enumerants = enumNode.getEnum().getEnumerants();
        KJ_REQUIRE(value.getEnum() < enumerants.size(),
                "Enum value out-of-range.", value.getEnum(), enumNode.getDisplayName());
        return kj::strTree(enumerants[value.getEnum()].getName());
      }
      case schema::Value::STRUCT: {
        KJ_REQUIRE(type.isStruct(), "type/value mismatch");
        auto structValue = value.getStruct().getAs<DynamicStruct>(
            scope.getDependency(type.getStruct().getTypeId()).asStruct());
        return kj::strTree(structValue);
      }
      case schema::Value::INTERFACE: {
        return kj::strTree("");
      }
      case schema::Value::OBJECT: {
        return kj::strTree("");
      }
    }
    return kj::strTree("");
  }

  kj::StringTree genAnnotation(schema::Annotation::Reader annotation,
                               Schema scope,
                               const char* prefix = " ", const char* suffix = "") {
    auto decl = schemaLoader.get(annotation.getId());
    auto proto = decl.getProto();
    KJ_REQUIRE(proto.isAnnotation());
    auto annDecl = proto.getAnnotation();

    auto value = genValue(annDecl.getType(), annotation.getValue(), decl).flatten();
    if (value.startsWith("(")) {
      return kj::strTree(prefix, "$", nodeName(decl, scope), value, suffix);
    } else {
      return kj::strTree(prefix, "$", nodeName(decl, scope), "(", value, ")", suffix);
    }
  }

  kj::StringTree genAnnotations(List<schema::Annotation>::Reader list, Schema scope) {
    return kj::strTree(KJ_MAP(ann, list) { return genAnnotation(ann, scope); });
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

  struct OrderByCodeOrder {
    template <typename T>
    inline bool operator()(const T& a, const T& b) const {
      return a.getProto().getCodeOrder() < b.getProto().getCodeOrder();
    }
  };

  template <typename MemberList>
  kj::Array<decltype(kj::instance<MemberList>()[0])> sortByCodeOrder(MemberList&& list) {
    auto sorted = KJ_MAP(item, list) { return item; };
    std::sort(sorted.begin(), sorted.end(), OrderByCodeOrder());
    return kj::mv(sorted);
  }

  kj::Array<kj::StringTree> genStructFields(StructSchema schema, Indent indent) {
    // Slightly hacky:  We want to print in code order, but we also need to print the union in one
    //   chunk.  Its fields should be together in code order anyway, but it's easier to simply
    //   output the whole union in place of the first union field, and then output nothing for the
    //   subsequent fields.

    bool seenUnion = false;
    return KJ_MAP(field, sortByCodeOrder(schema.getFields())) {
      if (field.getProto().hasDiscriminantValue()) {
        if (seenUnion) {
          return kj::strTree();
        } else {
          seenUnion = true;
          uint offset = schema.getProto().getStruct().getDiscriminantOffset();

          // GCC 4.7.3 crashes if you inline unionFields.
          auto unionFields = sortByCodeOrder(schema.getUnionFields());
          return kj::strTree(
              indent, "union {  # tag bits [", offset * 16, ", ", offset * 16 + 16, ")\n",
              KJ_MAP(uField, unionFields) {
                return genStructField(uField.getProto(), schema, indent.next());
              },
              indent, "}\n");
        }
      } else {
        return genStructField(field.getProto(), schema, indent);
      }
    };
  }

  kj::StringTree genStructField(schema::Field::Reader field, Schema scope, Indent indent) {
    switch (field.which()) {
      case schema::Field::SLOT: {
        auto slot = field.getSlot();
        int size = typeSizeBits(slot.getType());
        return kj::strTree(
            indent, field.getName(), " @", field.getOrdinal().getExplicit(),
            " :", genType(slot.getType(), scope),
            isEmptyValue(slot.getDefaultValue()) ? kj::strTree("") :
                kj::strTree(" = ", genValue(
                    slot.getType(), slot.getDefaultValue(), scope)),
            genAnnotations(field.getAnnotations(), scope),
            ";  # ", size == -1 ? kj::strTree("ptr[", slot.getOffset(), "]")
                                : kj::strTree("bits[", slot.getOffset() * size, ", ",
                                              (slot.getOffset() + 1) * size, ")"),
            field.hasDiscriminantValue()
                ? kj::strTree(", union tag = ", field.getDiscriminantValue()) : kj::strTree(),
            "\n");
      }
      case schema::Field::GROUP: {
        auto group = scope.getDependency(field.getGroup().getTypeId()).asStruct();
        return kj::strTree(
            indent, field.getName(),
            " :group", genAnnotations(field.getAnnotations(), scope), " {",
            field.hasDiscriminantValue()
                ? kj::strTree("  # union tag = ", field.getDiscriminantValue()) : kj::strTree(),
            "\n",
            genStructFields(group, indent.next()),
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

    switch (proto.which()) {
      case schema::Node::FILE:
        KJ_FAIL_REQUIRE("Encountered nested file node.");
        break;
      case schema::Node::STRUCT: {
        auto structProto = proto.getStruct();
        return kj::strTree(
            indent, "struct ", name,
            " @0x", kj::hex(proto.getId()), genAnnotations(schema), " {  # ",
            structProto.getDataWordCount() * 8, " bytes, ",
            structProto.getPointerCount(), " ptrs",
            structProto.getPreferredListEncoding() == schema::ElementSize::INLINE_COMPOSITE
                ? kj::strTree()
                : kj::strTree(", packed as ", elementSizeName(structProto.getPreferredListEncoding())),
            "\n",
            genStructFields(schema.asStruct(), indent.next()),
            genNestedDecls(schema, indent.next()),
            indent, "}\n");
      }
      case schema::Node::ENUM: {
        return kj::strTree(
            indent, "enum ", name, " @0x", kj::hex(proto.getId()), genAnnotations(schema), " {\n",
            KJ_MAP(enumerant, sortByCodeOrder(schema.asEnum().getEnumerants())) {
              return kj::strTree(indent.next(), enumerant.getProto().getName(), " @",
                                 enumerant.getIndex(),
                                 genAnnotations(enumerant.getProto().getAnnotations(), schema),
                                 ";\n");
            },
            genNestedDecls(schema, indent.next()),
            indent, "}\n");
      }
      case schema::Node::INTERFACE: {
        return kj::strTree(
            indent, "interface ", name, " @0x", kj::hex(proto.getId()),
            genAnnotations(schema), " {\n",
            KJ_MAP(method, sortByCodeOrder(schema.asInterface().getMethods())) {
              int i = 0;
              auto methodProto = method.getProto();
              return kj::strTree(
                  indent.next(), methodProto.getName(), " @", method.getIndex(), "(",
                  KJ_MAP(param, methodProto.getParams()) {
                    bool hasDefault = i >= methodProto.getRequiredParamCount() ||
                        !isEmptyValue(param.getDefaultValue());
                    return kj::strTree(
                        i++ > 0 ? ", " : "",
                        param.getName(), ": ", genType(param.getType(), schema),
                        hasDefault
                            ? kj::strTree(" = ", genValue(
                                param.getType(), param.getDefaultValue(), schema))
                            : kj::strTree(),
                        genAnnotations(param.getAnnotations(), schema));
                  },
                  ") :", genType(methodProto.getReturnType(), schema),
                  genAnnotations(methodProto.getAnnotations(), schema), ";\n");
            },
            genNestedDecls(schema, indent.next()),
            indent, "}\n");
      }
      case schema::Node::CONST: {
        auto constProto = proto.getConst();
        return kj::strTree(
            indent, "const ", name, " @0x", kj::hex(proto.getId()), " :",
            genType(constProto.getType(), schema), " = ",
            genValue(constProto.getType(), constProto.getValue(), schema), ";\n");
      }
      case schema::Node::ANNOTATION: {
        auto annotationProto = proto.getAnnotation();

        kj::Vector<kj::String> targets(8);
        bool targetsAll = true;

        auto dynamic = toDynamic(annotationProto);
        for (auto field: dynamic.getSchema().getFields()) {
          auto fieldName = field.getProto().getName();
          if (fieldName.startsWith("targets")) {
            if (dynamic.get(field).as<bool>()) {
              auto target = kj::str(fieldName.slice(strlen("targets")));
              target[0] = target[0] - 'A' + 'a';
              targets.add(kj::mv(target));
            } else {
              targetsAll = false;
            }
          }
        }

        if (targetsAll) {
          targets = kj::Vector<kj::String>(1);
          targets[0] = kj::heapString("*");
        }

        return kj::strTree(
            indent, "annotation ", name, " @0x", kj::hex(proto.getId()),
            " (", strArray(targets, ", "), ") :",
            genType(annotationProto.getType(), schema), genAnnotations(schema), ";\n");
      }
    }

    return kj::strTree();
  }

  kj::StringTree genNestedDecls(Schema schema, Indent indent) {
    uint64_t id = schema.getProto().getId();
    return kj::strTree(KJ_MAP(nested, schema.getProto().getNestedNodes()) {
      return genDecl(schemaLoader.get(nested.getId()), nested.getName(), id, indent);
    });
  }

  kj::StringTree genFile(Schema file) {
    auto proto = file.getProto();
    KJ_REQUIRE(proto.isFile(), "Expected a file node.", (uint)proto.which());

    return kj::strTree(
      "# ", proto.getDisplayName(), "\n",
      "@0x", kj::hex(proto.getId()), ";\n",
      KJ_MAP(ann, proto.getAnnotations()) { return genAnnotation(ann, file, "", ";\n"); },
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

    for (auto requestedFile: request.getRequestedFiles()) {
      genFile(schemaLoader.get(requestedFile.getId())).visit(
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
