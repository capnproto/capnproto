// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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
#include <kj/miniposix.h>
#include <unordered_map>
#include <kj/main.h>
#include <algorithm>
#include <map>

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace {

bool hasDiscriminantValue(const schema::Field::Reader& reader) {
  return reader.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT;
}

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

  kj::StringTree nodeName(Schema target, Schema scope, schema::Brand::Reader brand,
                          kj::Maybe<InterfaceSchema::Method> method) {
    kj::Vector<Schema> targetPath;
    kj::Vector<Schema> scopeParts;

    targetPath.add(target);

    std::map<uint64_t, List<schema::Brand::Binding>::Reader> scopeBindings;
    for (auto scopeBrand: brand.getScopes()) {
      switch (scopeBrand.which()) {
        case schema::Brand::Scope::BIND:
          scopeBindings[scopeBrand.getScopeId()] = scopeBrand.getBind();
          break;
        case schema::Brand::Scope::INHERIT:
          // TODO(someday): We need to pay attention to INHERIT and be sure to explicitly override
          //   any bindings that are not inherited. This requires a way to determine which of our
          //   parent scopes have a non-empty parameter list.
          break;
      }
    }

    {
      Schema parent = target;
      while (parent.getProto().getScopeId() != 0) {
        parent = schemaLoader.get(parent.getProto().getScopeId());
        targetPath.add(parent);
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

    // Remove common scope (unless it has been reparameterized).
    // TODO(someday):  This is broken in that we aren't checking for shadowing.
    while (!scopeParts.empty() && targetPath.size() > 1 &&
           scopeParts.back() == targetPath.back() &&
           scopeBindings.count(scopeParts.back().getProto().getId()) == 0) {
      scopeParts.removeLast();
      targetPath.removeLast();
    }

    auto parts = kj::heapArrayBuilder<kj::StringTree>(targetPath.size());
    while (!targetPath.empty()) {
      auto part = targetPath.back();
      auto proto = part.getProto();
      kj::StringTree partStr;
      if (proto.getScopeId() == 0) {
        partStr = kj::strTree("import \"/", proto.getDisplayName(), '\"');
      } else {
        partStr = kj::strTree(getUnqualifiedName(part));
      }

      auto iter = scopeBindings.find(proto.getId());
      if (iter != scopeBindings.end()) {
        auto bindings = KJ_MAP(binding, iter->second) {
          switch (binding.which()) {
            case schema::Brand::Binding::UNBOUND:
              return kj::strTree("AnyPointer");
            case schema::Brand::Binding::TYPE:
              return genType(binding.getType(), scope, method);
          }
          return kj::strTree("<unknown binding>");
        };
        partStr = kj::strTree(kj::mv(partStr), "(", kj::StringTree(kj::mv(bindings), ", "), ")");
      }

      parts.add(kj::mv(partStr));
      targetPath.removeLast();
    }

    return kj::StringTree(parts.finish(), ".");
  }

  kj::StringTree genType(schema::Type::Reader type, Schema scope,
                         kj::Maybe<InterfaceSchema::Method> method) {
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
        return kj::strTree("List(", genType(type.getList().getElementType(), scope, method), ")");
      case schema::Type::ENUM:
        return nodeName(schemaLoader.get(type.getEnum().getTypeId()), scope,
                        type.getEnum().getBrand(), method);
      case schema::Type::STRUCT:
        return nodeName(schemaLoader.get(type.getStruct().getTypeId()), scope,
                        type.getStruct().getBrand(), method);
      case schema::Type::INTERFACE:
        return nodeName(schemaLoader.get(type.getInterface().getTypeId()), scope,
                        type.getInterface().getBrand(), method);
      case schema::Type::ANY_POINTER: {
        auto anyPointer = type.getAnyPointer();
        switch (anyPointer.which()) {
          case schema::Type::AnyPointer::UNCONSTRAINED:
            switch (anyPointer.getUnconstrained().which()) {
              case schema::Type::AnyPointer::Unconstrained::ANY_KIND:
                return kj::strTree("AnyPointer");
              case schema::Type::AnyPointer::Unconstrained::STRUCT:
                return kj::strTree("AnyStruct");
              case schema::Type::AnyPointer::Unconstrained::LIST:
                return kj::strTree("AnyList");
              case schema::Type::AnyPointer::Unconstrained::CAPABILITY:
                return kj::strTree("Capability");
            }
            KJ_UNREACHABLE;
          case schema::Type::AnyPointer::PARAMETER: {
            auto param = anyPointer.getParameter();
            auto scopeProto = scope.getProto();
            auto targetScopeId = param.getScopeId();
            while (scopeProto.getId() != targetScopeId) {
              scopeProto = schemaLoader.get(param.getScopeId()).getProto();
            }
            auto params = scopeProto.getParameters();
            KJ_REQUIRE(param.getParameterIndex() < params.size());
            return kj::strTree(params[param.getParameterIndex()].getName());
          }
          case schema::Type::AnyPointer::IMPLICIT_METHOD_PARAMETER: {
            auto params = KJ_REQUIRE_NONNULL(method).getProto().getImplicitParameters();
            uint index = anyPointer.getImplicitMethodParameter().getParameterIndex();
            KJ_REQUIRE(index < params.size());
            return kj::strTree(params[index].getName());
          }
        }
        KJ_UNREACHABLE;
      }
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
      case schema::Type::ANY_POINTER: return -1;
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
      case schema::Value::ANY_POINTER: return true;
    }
    return true;
  }

  kj::StringTree genValue(Type type, schema::Value::Reader value) {
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
        auto listValue = value.getList().getAs<DynamicList>(type.asList());
        return kj::strTree(listValue);
      }
      case schema::Value::ENUM: {
        auto enumNode = type.asEnum().getProto();
        auto enumerants = enumNode.getEnum().getEnumerants();
        KJ_REQUIRE(value.getEnum() < enumerants.size(),
                "Enum value out-of-range.", value.getEnum(), enumNode.getDisplayName());
        return kj::strTree(enumerants[value.getEnum()].getName());
      }
      case schema::Value::STRUCT: {
        KJ_REQUIRE(type.which() == schema::Type::STRUCT, "type/value mismatch");
        auto structValue = value.getStruct().getAs<DynamicStruct>(type.asStruct());
        return kj::strTree(structValue);
      }
      case schema::Value::INTERFACE: {
        return kj::strTree("");
      }
      case schema::Value::ANY_POINTER: {
        return kj::strTree("");
      }
    }
    return kj::strTree("");
  }

  kj::StringTree genGenericParams(List<schema::Node::Parameter>::Reader params, Schema scope) {
    if (params.size() == 0) {
      return kj::strTree();
    }

    return kj::strTree(" (", kj::StringTree(
        KJ_MAP(param, params) { return kj::strTree(param.getName()); }, ", "), ')');
  }
  kj::StringTree genGenericParams(Schema schema) {
    auto proto = schema.getProto();
    return genGenericParams(proto.getParameters(), schemaLoader.get(proto.getScopeId()));
  }

  kj::StringTree genAnnotation(schema::Annotation::Reader annotation,
                               Schema scope,
                               const char* prefix = " ", const char* suffix = "") {
    auto decl = schemaLoader.get(annotation.getId(), annotation.getBrand(), scope);
    auto proto = decl.getProto();
    KJ_REQUIRE(proto.isAnnotation());
    auto annDecl = proto.getAnnotation();

    auto value = genValue(schemaLoader.getType(annDecl.getType(), decl),
                          annotation.getValue()).flatten();
    if (value.startsWith("(")) {
      return kj::strTree(prefix, "$", nodeName(decl, scope, annotation.getBrand(), nullptr),
                         value, suffix);
    } else {
      return kj::strTree(prefix, "$", nodeName(decl, scope, annotation.getBrand(), nullptr),
                         "(", value, ")", suffix);
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
      if (hasDiscriminantValue(field.getProto())) {
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
                return genStructField(uField, schema, indent.next());
              },
              indent, "}\n");
        }
      } else {
        return genStructField(field, schema, indent);
      }
    };
  }

  kj::StringTree genStructField(StructSchema::Field field, Schema scope, Indent indent) {
    auto proto = field.getProto();
    switch (proto.which()) {
      case schema::Field::SLOT: {
        auto slot = proto.getSlot();
        int size = typeSizeBits(slot.getType());
        return kj::strTree(
            indent, proto.getName(), " @", proto.getOrdinal().getExplicit(),
            " :", genType(slot.getType(), scope, nullptr),
            isEmptyValue(slot.getDefaultValue()) ? kj::strTree("") :
                kj::strTree(" = ", genValue(field.getType(), slot.getDefaultValue())),
            genAnnotations(proto.getAnnotations(), scope),
            ";  # ", size == -1 ? kj::strTree("ptr[", slot.getOffset(), "]")
                                : kj::strTree("bits[", slot.getOffset() * size, ", ",
                                              (slot.getOffset() + 1) * size, ")"),
            hasDiscriminantValue(proto)
                ? kj::strTree(", union tag = ", proto.getDiscriminantValue()) : kj::strTree(),
            "\n");
      }
      case schema::Field::GROUP: {
        auto group = field.getType().asStruct();
        return kj::strTree(
            indent, proto.getName(),
            " :group", genAnnotations(proto.getAnnotations(), scope), " {",
            hasDiscriminantValue(proto)
                ? kj::strTree("  # union tag = ", proto.getDiscriminantValue()) : kj::strTree(),
            "\n",
            genStructFields(group, indent.next()),
            indent, "}\n");
      }
    }
    return kj::strTree();
  }

  kj::StringTree genParamList(InterfaceSchema interface, StructSchema schema,
                              schema::Brand::Reader brand, InterfaceSchema::Method method) {
    if (schema.getProto().getScopeId() == 0) {
      // A named parameter list.
      return kj::strTree("(", kj::StringTree(
          KJ_MAP(field, schema.getFields()) {
            auto proto = field.getProto();
            auto slot = proto.getSlot();

            return kj::strTree(
                proto.getName(), " :", genType(slot.getType(), interface, nullptr),
                isEmptyValue(slot.getDefaultValue()) ? kj::strTree("") :
                    kj::strTree(" = ", genValue(field.getType(), slot.getDefaultValue())),
                genAnnotations(proto.getAnnotations(), interface));
          }, ", "), ")");
    } else {
      return nodeName(schema, interface, brand, method);
    }
  }

  kj::StringTree genSuperclasses(InterfaceSchema interface) {
    auto superclasses = interface.getProto().getInterface().getSuperclasses();
    if (superclasses.size() == 0) {
      return kj::strTree();
    } else {
      return kj::strTree(" superclasses(", kj::StringTree(
          KJ_MAP(superclass, superclasses) {
            return nodeName(schemaLoader.get(superclass.getId()), interface,
                            superclass.getBrand(), nullptr);
          }, ", "), ")");
    }
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
            " @0x", kj::hex(proto.getId()), genGenericParams(schema),
            genAnnotations(schema), " {  # ",
            structProto.getDataWordCount() * 8, " bytes, ",
            structProto.getPointerCount(), " ptrs",
            structProto.getPreferredListEncoding() == schema::ElementSize::INLINE_COMPOSITE
                ? kj::strTree()
                : kj::strTree(", packed as ",
                              elementSizeName(structProto.getPreferredListEncoding())),
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
        auto interface = schema.asInterface();
        return kj::strTree(
            indent, "interface ", name, " @0x", kj::hex(proto.getId()), genGenericParams(schema),
            genSuperclasses(interface),
            genAnnotations(schema), " {\n",
            KJ_MAP(method, sortByCodeOrder(interface.getMethods())) {
              auto methodProto = method.getProto();

              auto implicits = methodProto.getImplicitParameters();
              kj::StringTree implicitsStr;
              if (implicits.size() > 0) {
                implicitsStr = kj::strTree(
                    "[", kj::StringTree(KJ_MAP(implicit, implicits) {
                      return kj::strTree(implicit.getName());
                    }, ", "), "] ");
              }

              auto params = schemaLoader.get(methodProto.getParamStructType()).asStruct();
              auto results = schemaLoader.get(methodProto.getResultStructType()).asStruct();
              return kj::strTree(
                  indent.next(), methodProto.getName(),
                  " @", method.getIndex(), " ", kj::mv(implicitsStr),
                  genParamList(interface, params, methodProto.getParamBrand(), method), " -> ",
                  genParamList(interface, results, methodProto.getResultBrand(), method),
                  genAnnotations(methodProto.getAnnotations(), interface), ";\n");
            },
            genNestedDecls(schema, indent.next()),
            indent, "}\n");
      }
      case schema::Node::CONST: {
        auto constProto = proto.getConst();
        return kj::strTree(
            indent, "const ", name, " @0x", kj::hex(proto.getId()), " :",
            genType(constProto.getType(), schema, nullptr), " = ",
            genValue(schema.asConst().getType(), constProto.getValue()),
            genAnnotations(schema), ";\n");
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
          targets.add(kj::heapString("*"));
        }

        return kj::strTree(
            indent, "annotation ", name, " @0x", kj::hex(proto.getId()),
            " (", strArray(targets, ", "), ") :",
            genType(annotationProto.getType(), schema, nullptr), genAnnotations(schema), ";\n");
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
