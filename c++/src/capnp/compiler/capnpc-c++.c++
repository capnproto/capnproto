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

// This program is a code generator plugin for `capnp compile` which generates C++ code.

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
#include <unordered_set>
#include <set>
#include <kj/main.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace {

static constexpr uint64_t NAMESPACE_ANNOTATION_ID = 0xb9c6f99ebf805f2cull;

static constexpr const char* FIELD_SIZE_NAMES[] = {
  "VOID", "BIT", "BYTE", "TWO_BYTES", "FOUR_BYTES", "EIGHT_BYTES", "POINTER", "INLINE_COMPOSITE"
};

void enumerateDeps(schema::Type::Reader type, std::set<uint64_t>& deps) {
  switch (type.which()) {
    case schema::Type::STRUCT:
      deps.insert(type.getStruct().getTypeId());
      break;
    case schema::Type::ENUM:
      deps.insert(type.getEnum().getTypeId());
      break;
    case schema::Type::INTERFACE:
      deps.insert(type.getInterface().getTypeId());
      break;
    case schema::Type::LIST:
      enumerateDeps(type.getList().getElementType(), deps);
      break;
    default:
      break;
  }
}

void enumerateDeps(schema::Node::Reader node, std::set<uint64_t>& deps) {
  switch (node.which()) {
    case schema::Node::STRUCT: {
      auto structNode = node.getStruct();
      for (auto field: structNode.getFields()) {
        switch (field.which()) {
          case schema::Field::SLOT:
            enumerateDeps(field.getSlot().getType(), deps);
            break;
          case schema::Field::GROUP:
            deps.insert(field.getGroup().getTypeId());
            break;
        }
      }
      if (structNode.getIsGroup()) {
        deps.insert(node.getScopeId());
      }
      break;
    }
    case schema::Node::INTERFACE:
      for (auto method: node.getInterface().getMethods()) {
        for (auto param: method.getParams()) {
          enumerateDeps(param.getType(), deps);
        }
        enumerateDeps(method.getReturnType(), deps);
      }
      break;
    default:
      break;
  }
}

struct OrderByName {
  template <typename T>
  inline bool operator()(const T& a, const T& b) const {
    return a.getProto().getName() < b.getProto().getName();
  }
};

template <typename MemberList>
kj::Array<uint> makeMembersByName(MemberList&& members) {
  auto sorted = KJ_MAP(member, members) { return member; };
  std::sort(sorted.begin(), sorted.end(), OrderByName());
  return KJ_MAP(member, sorted) { return member.getIndex(); };
}

kj::StringPtr baseName(kj::StringPtr path) {
  KJ_IF_MAYBE(slashPos, path.findLast('/')) {
    return path.slice(*slashPos + 1);
  } else {
    return path;
  }
}

// =======================================================================================

class CapnpcCppMain {
public:
  CapnpcCppMain(kj::ProcessContext& context): context(context) {}

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
  std::unordered_set<uint64_t> usedImports;

  kj::StringTree cppFullName(Schema schema) {
    auto node = schema.getProto();
    if (node.getScopeId() == 0) {
      usedImports.insert(node.getId());
      for (auto annotation: node.getAnnotations()) {
        if (annotation.getId() == NAMESPACE_ANNOTATION_ID) {
          return kj::strTree(" ::", annotation.getValue().getText());
        }
      }
      return kj::strTree(" ");
    } else {
      Schema parent = schemaLoader.get(node.getScopeId());
      for (auto nested: parent.getProto().getNestedNodes()) {
        if (nested.getId() == node.getId()) {
          return kj::strTree(cppFullName(parent), "::", nested.getName());
        }
      }
      KJ_FAIL_REQUIRE("A schema Node's supposed scope did not contain the node as a NestedNode.");
    }
  }

  kj::String toUpperCase(kj::StringPtr name) {
    kj::Vector<char> result(name.size() + 4);

    for (char c: name) {
      if ('a' <= c && c <= 'z') {
        result.add(c - 'a' + 'A');
      } else if (result.size() > 0 && 'A' <= c && c <= 'Z') {
        result.add('_');
        result.add(c);
      } else {
        result.add(c);
      }
    }

    result.add('\0');

    return kj::String(result.releaseAsArray());
  }

  kj::String toTitleCase(kj::StringPtr name) {
    kj::String result = kj::heapString(name);
    if ('a' <= result[0] && result[0] <= 'z') {
      result[0] = result[0] - 'a' + 'A';
    }
    return kj::mv(result);
  }

  kj::StringTree typeName(schema::Type::Reader type) {
    switch (type.which()) {
      case schema::Type::VOID: return kj::strTree(" ::capnp::Void");

      case schema::Type::BOOL: return kj::strTree("bool");
      case schema::Type::INT8: return kj::strTree(" ::int8_t");
      case schema::Type::INT16: return kj::strTree(" ::int16_t");
      case schema::Type::INT32: return kj::strTree(" ::int32_t");
      case schema::Type::INT64: return kj::strTree(" ::int64_t");
      case schema::Type::UINT8: return kj::strTree(" ::uint8_t");
      case schema::Type::UINT16: return kj::strTree(" ::uint16_t");
      case schema::Type::UINT32: return kj::strTree(" ::uint32_t");
      case schema::Type::UINT64: return kj::strTree(" ::uint64_t");
      case schema::Type::FLOAT32: return kj::strTree("float");
      case schema::Type::FLOAT64: return kj::strTree("double");

      case schema::Type::TEXT: return kj::strTree(" ::capnp::Text");
      case schema::Type::DATA: return kj::strTree(" ::capnp::Data");

      case schema::Type::ENUM:
        return cppFullName(schemaLoader.get(type.getEnum().getTypeId()));
      case schema::Type::STRUCT:
        return cppFullName(schemaLoader.get(type.getStruct().getTypeId()));
      case schema::Type::INTERFACE:
        return cppFullName(schemaLoader.get(type.getInterface().getTypeId()));

      case schema::Type::LIST:
        return kj::strTree(" ::capnp::List<", typeName(type.getList().getElementType()), ">");

      case schema::Type::OBJECT:
        // Not used.
        return kj::strTree();
    }
    KJ_UNREACHABLE;
  }

  kj::StringTree literalValue(schema::Type::Reader type, schema::Value::Reader value) {
    switch (value.which()) {
      case schema::Value::VOID: return kj::strTree(" ::capnp::VOID");
      case schema::Value::BOOL: return kj::strTree(value.getBool() ? "true" : "false");
      case schema::Value::INT8: return kj::strTree(value.getInt8());
      case schema::Value::INT16: return kj::strTree(value.getInt16());
      case schema::Value::INT32: return kj::strTree(value.getInt32());
      case schema::Value::INT64: return kj::strTree(value.getInt64(), "ll");
      case schema::Value::UINT8: return kj::strTree(value.getUint8(), "u");
      case schema::Value::UINT16: return kj::strTree(value.getUint16(), "u");
      case schema::Value::UINT32: return kj::strTree(value.getUint32(), "u");
      case schema::Value::UINT64: return kj::strTree(value.getUint64(), "llu");
      case schema::Value::FLOAT32: return kj::strTree(value.getFloat32(), "f");
      case schema::Value::FLOAT64: return kj::strTree(value.getFloat64());
      case schema::Value::ENUM: {
        EnumSchema schema = schemaLoader.get(type.getEnum().getTypeId()).asEnum();
        if (value.getEnum() < schema.getEnumerants().size()) {
          return kj::strTree(
              cppFullName(schema), "::",
              toUpperCase(schema.getEnumerants()[value.getEnum()].getProto().getName()));
        } else {
          return kj::strTree("static_cast<", cppFullName(schema), ">(", value.getEnum(), ")");
        }
      }

      case schema::Value::TEXT:
      case schema::Value::DATA:
      case schema::Value::STRUCT:
      case schema::Value::INTERFACE:
      case schema::Value::LIST:
      case schema::Value::OBJECT:
        KJ_FAIL_REQUIRE("literalValue() can only be used on primitive types.");
    }
    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------
  // Code to deal with "slots" -- determines what to zero out when we clear a group.

  static uint typeSizeBits(schema::Type::Which whichType) {
    switch (whichType) {
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
      case schema::Type::ENUM: return 16;

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::OBJECT:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }

  enum class Section {
    NONE,
    DATA,
    POINTERS
  };

  static Section sectionFor(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::VOID:
        return Section::NONE;
      case schema::Type::BOOL:
      case schema::Type::INT8:
      case schema::Type::INT16:
      case schema::Type::INT32:
      case schema::Type::INT64:
      case schema::Type::UINT8:
      case schema::Type::UINT16:
      case schema::Type::UINT32:
      case schema::Type::UINT64:
      case schema::Type::FLOAT32:
      case schema::Type::FLOAT64:
      case schema::Type::ENUM:
        return Section::DATA;
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::OBJECT:
        return Section::POINTERS;
    }
    KJ_UNREACHABLE;
  }

  static kj::StringPtr maskType(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::BOOL: return "bool";
      case schema::Type::INT8: return " ::uint8_t";
      case schema::Type::INT16: return " ::uint16_t";
      case schema::Type::INT32: return " ::uint32_t";
      case schema::Type::INT64: return " ::uint64_t";
      case schema::Type::UINT8: return " ::uint8_t";
      case schema::Type::UINT16: return " ::uint16_t";
      case schema::Type::UINT32: return " ::uint32_t";
      case schema::Type::UINT64: return " ::uint64_t";
      case schema::Type::FLOAT32: return " ::uint32_t";
      case schema::Type::FLOAT64: return " ::uint64_t";
      case schema::Type::ENUM: return " ::uint16_t";

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::OBJECT:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }

  struct Slot {
    schema::Type::Which whichType;
    uint offset;

    bool isSupersetOf(Slot other) const {
      auto section = sectionFor(whichType);
      if (section != sectionFor(other.whichType)) return false;
      switch (section) {
        case Section::NONE:
          return true;  // all voids overlap
        case Section::DATA: {
          auto bits = typeSizeBits(whichType);
          auto start = offset * bits;
          auto otherBits = typeSizeBits(other.whichType);
          auto otherStart = other.offset * otherBits;
          return start <= otherStart && otherStart + otherBits <= start + bits;
        }
        case Section::POINTERS:
          return offset == other.offset;
      }
      KJ_UNREACHABLE;
    }

    bool operator<(Slot other) const {
      // Sort by section, then start position, and finally size.

      auto section = sectionFor(whichType);
      auto otherSection = sectionFor(other.whichType);
      if (section < otherSection) {
        return true;
      } else if (section > otherSection) {
        return false;
      }

      switch (section) {
        case Section::NONE:
          return false;
        case Section::DATA: {
          auto bits = typeSizeBits(whichType);
          auto start = offset * bits;
          auto otherBits = typeSizeBits(other.whichType);
          auto otherStart = other.offset * otherBits;
          if (start < otherStart) {
            return true;
          } else if (start > otherStart) {
            return false;
          }

          // Sort larger sizes before smaller.
          return bits > otherBits;
        }
        case Section::POINTERS:
          return offset < other.offset;
      }
      KJ_UNREACHABLE;
    }
  };

  void getSlots(StructSchema schema, kj::Vector<Slot>& slots) {
    auto structProto = schema.getProto().getStruct();
    if (structProto.getDiscriminantCount() > 0) {
      slots.add(Slot { schema::Type::UINT16, structProto.getDiscriminantOffset() });
    }

    for (auto field: schema.getFields()) {
      auto proto = field.getProto();
      switch (proto.which()) {
        case schema::Field::SLOT: {
          auto slot = proto.getSlot();
          slots.add(Slot { slot.getType().which(), slot.getOffset() });
          break;
        }
        case schema::Field::GROUP:
          getSlots(schema.getDependency(proto.getGroup().getTypeId()).asStruct(), slots);
          break;
      }
    }
  }

  kj::Array<Slot> getSortedSlots(StructSchema schema) {
    // Get a representation of all of the field locations owned by this schema, e.g. so that they
    // can be zero'd out.

    kj::Vector<Slot> slots(schema.getFields().size());
    getSlots(schema, slots);
    std::sort(slots.begin(), slots.end());

    kj::Vector<Slot> result(slots.size());

    // All void slots are redundant, and they sort towards the front of the list.  By starting out
    // with `prevSlot` = void, we will end up skipping them all, which is what we want.
    Slot prevSlot = { schema::Type::VOID, 0 };
    for (auto slot: slots) {
      if (prevSlot.isSupersetOf(slot)) {
        // This slot is redundant as prevSlot is a superset of it.
        continue;
      }

      // Since all sizes are power-of-two, if two slots overlap at all, one must be a superset of
      // the other.  Since we sort slots by starting position, we know that the only way `slot`
      // could be a superset of `prevSlot` is if they have the same starting position.  However,
      // since we sort slots with the same starting position by descending size, this is not
      // possible.
      KJ_DASSERT(!slot.isSupersetOf(prevSlot));

      result.add(slot);

      prevSlot = slot;
    }

    return result.releaseAsArray();
  }

  // -----------------------------------------------------------------

  struct DiscriminantChecks {
    kj::String has;
    kj::String check;
    kj::String set;
    kj::StringTree readerIsDecl;
    kj::StringTree builderIsDecl;
    kj::StringTree isDefs;
  };

  DiscriminantChecks makeDiscriminantChecks(kj::StringPtr scope,
                                            kj::StringPtr memberName,
                                            StructSchema containingStruct) {
    auto discrimOffset = containingStruct.getProto().getStruct().getDiscriminantOffset();

    kj::String titleCase = toTitleCase(memberName);
    kj::String upperCase = toUpperCase(memberName);

    return DiscriminantChecks {
        kj::str(
            "  if (which() != ", scope, upperCase, ") return false;\n"),
        kj::str(
            "  KJ_IREQUIRE(which() == ", scope, upperCase, ",\n"
            "              \"Must check which() before get()ing a union member.\");\n"),
        kj::str(
            "  _builder.setDataField<", scope, "Which>(\n"
            "      ", discrimOffset, " * ::capnp::ELEMENTS, ",
                      scope, upperCase, ");\n"),
        kj::strTree("  inline bool is", titleCase, "() const;\n"),
        kj::strTree("  inline bool is", titleCase, "();\n"),
        kj::strTree(
            "inline bool ", scope, "Reader::is", titleCase, "() const {\n"
            "  return which() == ", scope, upperCase, ";\n"
            "}\n"
            "inline bool ", scope, "Builder::is", titleCase, "() {\n"
            "  return which() == ", scope, upperCase, ";\n"
            "}\n")
    };
  }

  // -----------------------------------------------------------------

  struct FieldText {
    kj::StringTree readerMethodDecls;
    kj::StringTree builderMethodDecls;
    kj::StringTree inlineMethodDefs;
  };

  enum class FieldKind {
    PRIMITIVE,
    BLOB,
    STRUCT,
    LIST,
    INTERFACE,
    OBJECT
  };

  FieldText makeFieldText(kj::StringPtr scope, StructSchema::Field field) {
    auto proto = field.getProto();
    kj::String titleCase = toTitleCase(proto.getName());

    DiscriminantChecks unionDiscrim;
    if (proto.hasDiscriminantValue()) {
      unionDiscrim = makeDiscriminantChecks(scope, proto.getName(), field.getContainingStruct());
    }

    switch (proto.which()) {
      case schema::Field::SLOT:
        // Continue below.
        break;

      case schema::Field::GROUP: {
        auto slots = getSortedSlots(schemaLoader.get(
            field.getProto().getGroup().getTypeId()).asStruct());
        return FieldText {
            kj::strTree(
                kj::mv(unionDiscrim.readerIsDecl),
                "  inline bool has", titleCase, "() const;\n"
                "  inline ", titleCase, "::Reader get", titleCase, "() const;\n"
                "\n"),

            kj::strTree(
                kj::mv(unionDiscrim.builderIsDecl),
                "  inline bool has", titleCase, "();\n"
                "  inline ", titleCase, "::Builder get", titleCase, "();\n"
                "  inline ", titleCase, "::Builder init", titleCase, "();\n"
                "\n"),

            kj::strTree(
                kj::mv(unionDiscrim.isDefs),
                "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
                unionDiscrim.has,
                "  return ",
                kj::StringTree(KJ_MAP(slot, slots) {
                  switch (sectionFor(slot.whichType)) {
                    case Section::NONE:
                      return kj::strTree();
                    case Section::DATA:
                      return kj::strTree(
                          "_reader.getDataField<", maskType(slot.whichType), ">(",
                              slot.offset, " * ::capnp::ELEMENTS) != 0");
                    case Section::POINTERS:
                      return kj::strTree(
                          "!_reader.isPointerFieldNull(", slot.offset, " * ::capnp::POINTERS)");
                  }
                  KJ_UNREACHABLE;
                }, "\n      || "), ";\n"
                "}\n"
                "inline bool ", scope, "Builder::has", titleCase, "() {\n",
                unionDiscrim.has,
                "  return ",
                kj::StringTree(KJ_MAP(slot, slots) {
                  switch (sectionFor(slot.whichType)) {
                    case Section::NONE:
                      return kj::strTree();
                    case Section::DATA:
                      return kj::strTree(
                          "_builder.getDataField<", maskType(slot.whichType), ">(",
                              slot.offset, " * ::capnp::ELEMENTS) != 0");
                    case Section::POINTERS:
                      return kj::strTree(
                          "!_builder.isPointerFieldNull(", slot.offset, " * ::capnp::POINTERS)");
                  }
                  KJ_UNREACHABLE;
                }, "\n      || "), ";\n"
                "}\n"
                "inline ", scope, titleCase, "::Reader ", scope, "Reader::get", titleCase, "() const {\n",
                unionDiscrim.check,
                "  return ", scope, titleCase, "::Reader(_reader);\n"
                "}\n"
                "inline ", scope, titleCase, "::Builder ", scope, "Builder::get", titleCase, "() {\n",
                unionDiscrim.check,
                "  return ", scope, titleCase, "::Builder(_builder);\n"
                "}\n"
                "inline ", scope, titleCase, "::Builder ", scope, "Builder::init", titleCase, "() {\n",
                unionDiscrim.set,
                KJ_MAP(slot, slots) {
                  switch (sectionFor(slot.whichType)) {
                    case Section::NONE:
                      return kj::strTree();
                    case Section::DATA:
                      return kj::strTree(
                          "  _builder.setDataField<", maskType(slot.whichType), ">(",
                              slot.offset, " * ::capnp::ELEMENTS, 0);\n");
                    case Section::POINTERS:
                      return kj::strTree(
                          "  _builder.clearPointer(", slot.offset, " * ::capnp::POINTERS);\n");
                  }
                  KJ_UNREACHABLE;
                },
                "  return ", scope, titleCase, "::Builder(_builder);\n"
                "}\n")
          };
      }
    }

    auto slot = proto.getSlot();

    FieldKind kind = FieldKind::PRIMITIVE;
    kj::String ownedType;
    kj::String type = typeName(slot.getType()).flatten();
    kj::StringPtr setterDefault;  // only for void
    kj::String defaultMask;    // primitives only
    size_t defaultOffset = 0;    // pointers only: offset of the default value within the schema.
    size_t defaultSize = 0;      // blobs only: byte size of the default value.

    auto typeBody = slot.getType();
    auto defaultBody = slot.getDefaultValue();
    switch (typeBody.which()) {
      case schema::Type::VOID:
        kind = FieldKind::PRIMITIVE;
        setterDefault = " = ::capnp::VOID";
        break;

#define HANDLE_PRIMITIVE(discrim, typeName, defaultName, suffix) \
      case schema::Type::discrim: \
        kind = FieldKind::PRIMITIVE; \
        if (defaultBody.get##defaultName() != 0) { \
          defaultMask = kj::str(defaultBody.get##defaultName(), #suffix); \
        } \
        break;

      HANDLE_PRIMITIVE(BOOL, bool, Bool, );
      HANDLE_PRIMITIVE(INT8 , ::int8_t , Int8 , );
      HANDLE_PRIMITIVE(INT16, ::int16_t, Int16, );
      HANDLE_PRIMITIVE(INT32, ::int32_t, Int32, );
      HANDLE_PRIMITIVE(INT64, ::int64_t, Int64, ll);
      HANDLE_PRIMITIVE(UINT8 , ::uint8_t , Uint8 , u);
      HANDLE_PRIMITIVE(UINT16, ::uint16_t, Uint16, u);
      HANDLE_PRIMITIVE(UINT32, ::uint32_t, Uint32, u);
      HANDLE_PRIMITIVE(UINT64, ::uint64_t, Uint64, ull);
#undef HANDLE_PRIMITIVE

      case schema::Type::FLOAT32:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat32() != 0) {
          uint32_t mask;
          float value = defaultBody.getFloat32();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "u");
        }
        break;

      case schema::Type::FLOAT64:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat64() != 0) {
          uint64_t mask;
          double value = defaultBody.getFloat64();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "ull");
        }
        break;

      case schema::Type::TEXT:
        kind = FieldKind::BLOB;
        if (defaultBody.hasText()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getText().size();
        }
        break;
      case schema::Type::DATA:
        kind = FieldKind::BLOB;
        if (defaultBody.hasData()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getData().size();
        }
        break;

      case schema::Type::ENUM:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getEnum() != 0) {
          defaultMask = kj::str(defaultBody.getEnum(), "u");
        }
        break;

      case schema::Type::STRUCT:
        kind = FieldKind::STRUCT;
        if (defaultBody.hasStruct()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::LIST:
        kind = FieldKind::LIST;
        if (defaultBody.hasList()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::INTERFACE:
        kind = FieldKind::INTERFACE;
        break;
      case schema::Type::OBJECT:
        kind = FieldKind::OBJECT;
        if (defaultBody.hasObject()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
    }

    kj::String defaultMaskParam;
    if (defaultMask.size() > 0) {
      defaultMaskParam = kj::str(", ", defaultMask);
    }

    uint offset = slot.getOffset();

    if (kind == FieldKind::PRIMITIVE) {
      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline bool has", titleCase, "() const;\n"
            "  inline ", type, " get", titleCase, "() const;\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline bool has", titleCase, "();\n"
            "  inline ", type, " get", titleCase, "();\n"
            "  inline void set", titleCase, "(", type, " value", setterDefault, ");\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionDiscrim.has,
            "  return _reader.hasDataField<", type, ">(", offset, " * ::capnp::ELEMENTS);\n",
            "}\n"
            "\n"
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionDiscrim.has,
            "  return _builder.hasDataField<", type, ">(", offset, " * ::capnp::ELEMENTS);\n",
            "}\n"
            "inline ", type, " ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return _reader.getDataField<", type, ">(\n"
            "      ", offset, " * ::capnp::ELEMENTS", defaultMaskParam, ");\n",
            "}\n"
            "\n"
            "inline ", type, " ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return _builder.getDataField<", type, ">(\n"
            "      ", offset, " * ::capnp::ELEMENTS", defaultMaskParam, ");\n",
            "}\n"
            "inline void ", scope, "Builder::set", titleCase, "(", type, " value) {\n",
            unionDiscrim.set,
            "  _builder.setDataField<", type, ">(\n"
            "      ", offset, " * ::capnp::ELEMENTS, value", defaultMaskParam, ");\n",
            "}\n"
            "\n")
      };

    } else if (kind == FieldKind::INTERFACE) {
      // Not implemented.
      return FieldText { kj::strTree(), kj::strTree(), kj::strTree() };

    } else if (kind == FieldKind::OBJECT) {
      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline bool has", titleCase, "() const;\n"
            "  template <typename T>\n"
            "  inline typename T::Reader get", titleCase, "() const;\n"
            "  template <typename T, typename Param>\n"
            "  inline typename T::Reader get", titleCase, "(Param&& param) const;\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline bool has", titleCase, "();\n"
            "  template <typename T>\n"
            "  inline typename T::Builder get", titleCase, "();\n"
            "  template <typename T, typename Param>\n"
            "  inline typename T::Builder get", titleCase, "(Param&& param);\n"
            "  template <typename T>\n"
            "  inline void set", titleCase, "(typename T::Reader value);\n"
            "  template <typename T, typename U>"
            "  inline void set", titleCase, "(std::initializer_list<U> value);\n"
            "  template <typename T, typename... Params>\n"
            "  inline typename T::Builder init", titleCase, "(Params&&... params);\n"
            "  template <typename T>\n"
            "  inline void adopt", titleCase, "(::capnp::Orphan<T>&& value);\n"
            "  template <typename T, typename... Params>\n"
            "  inline ::capnp::Orphan<T> disown", titleCase, "(Params&&... params);\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionDiscrim.has,
            "  return !_reader.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionDiscrim.has,
            "  return !_builder.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "template <typename T>\n"
            "inline typename T::Reader ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<T>::get(\n"
            "      _reader, ", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "template <typename T>\n"
            "inline typename T::Builder ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<T>::get(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "template <typename T, typename Param>\n"
            "inline typename T::Reader ", scope, "Reader::get", titleCase, "(Param&& param) const {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<T>::getDynamic(\n"
            "      _reader, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Param>(param));\n"
            "}\n"
            "template <typename T, typename Param>\n"
            "inline typename T::Builder ", scope, "Builder::get", titleCase, "(Param&& param) {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<T>::getDynamic(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Param>(param));\n"
            "}\n"
            "template <typename T>\n"
            "inline void ", scope, "Builder::set", titleCase, "(typename T::Reader value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<T>::set(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
            "}\n"
            "template <typename T, typename U>"
            "inline void ", scope, "Builder::set", titleCase, "(std::initializer_list<U> value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<T>::set(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
            "}\n"
            "template <typename T, typename... Params>\n"
            "inline typename T::Builder ", scope, "Builder::init", titleCase, "(Params&&... params) {\n",
            unionDiscrim.set,
            "  return ::capnp::_::PointerHelpers<T>::init(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);\n"
            "}\n"
            "template <typename T>\n"
            "inline void ", scope, "Builder::adopt", titleCase, "(::capnp::Orphan<T>&& value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<T>::adopt(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, kj::mv(value));\n"
            "}\n"
            "template <typename T, typename... Params>\n"
            "inline ::capnp::Orphan<T> ", scope, "Builder::disown", titleCase, "(Params&&... params) {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<T>::disown(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);\n"
            "}\n"
            "\n")
      };

    } else {
      // Blob, struct, or list.  These have only minor differences.

      uint64_t typeId = field.getContainingStruct().getProto().getId();
      kj::String defaultParam = defaultOffset == 0 ? kj::str() : kj::str(
          ",\n        ::capnp::schemas::s_", kj::hex(typeId), ".encodedNode + ", defaultOffset,
          defaultSize == 0 ? kj::strTree() : kj::strTree(", ", defaultSize));

      kj::String elementReaderType;
      bool isStructList = false;
      if (kind == FieldKind::LIST) {
        bool primitiveElement = false;
        switch (typeBody.getList().getElementType().which()) {
          case schema::Type::VOID:
          case schema::Type::BOOL:
          case schema::Type::INT8:
          case schema::Type::INT16:
          case schema::Type::INT32:
          case schema::Type::INT64:
          case schema::Type::UINT8:
          case schema::Type::UINT16:
          case schema::Type::UINT32:
          case schema::Type::UINT64:
          case schema::Type::FLOAT32:
          case schema::Type::FLOAT64:
          case schema::Type::ENUM:
            primitiveElement = true;
            break;

          case schema::Type::TEXT:
          case schema::Type::DATA:
          case schema::Type::LIST:
          case schema::Type::INTERFACE:
          case schema::Type::OBJECT:
            primitiveElement = false;
            break;

          case schema::Type::STRUCT:
            isStructList = true;
            primitiveElement = false;
            break;
        }
        elementReaderType = kj::str(
            typeName(typeBody.getList().getElementType()),
            primitiveElement ? "" : "::Reader");
      }

      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline bool has", titleCase, "() const;\n"
            "  inline ", type, "::Reader get", titleCase, "() const;\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline bool has", titleCase, "();\n"
            "  inline ", type, "::Builder get", titleCase, "();\n"
            "  inline void set", titleCase, "(", type, "::Reader value);\n",
            kind == FieldKind::LIST && !isStructList
            ? kj::strTree(
              "  inline void set", titleCase, "(std::initializer_list<", elementReaderType, "> value);\n")
            : kj::strTree(),
            kind == FieldKind::STRUCT
            ? kj::strTree(
              "  inline ", type, "::Builder init", titleCase, "();\n")
            : kj::strTree(
              "  inline ", type, "::Builder init", titleCase, "(unsigned int size);\n"),
            "  inline void adopt", titleCase, "(::capnp::Orphan<", type, ">&& value);\n"
            "  inline ::capnp::Orphan<", type, "> disown", titleCase, "();\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionDiscrim.has,
            "  return !_reader.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionDiscrim.has,
            "  return !_builder.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "inline ", type, "::Reader ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(\n"
            "      _reader, ", offset, " * ::capnp::POINTERS", defaultParam, ");\n"
            "}\n"
            "inline ", type, "::Builder ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS", defaultParam, ");\n"
            "}\n"
            "inline void ", scope, "Builder::set", titleCase, "(", type, "::Reader value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::set(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
            "}\n",
            kind == FieldKind::LIST && !isStructList
            ? kj::strTree(
              "inline void ", scope, "Builder::set", titleCase, "(std::initializer_list<", elementReaderType, "> value) {\n",
              unionDiscrim.set,
              "  ::capnp::_::PointerHelpers<", type, ">::set(\n"
              "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
              "}\n")
            : kj::strTree(),
            kind == FieldKind::STRUCT
            ? kj::strTree(
                "inline ", type, "::Builder ", scope, "Builder::init", titleCase, "() {\n",
                unionDiscrim.set,
                "  return ::capnp::_::PointerHelpers<", type, ">::init(\n"
                "      _builder, ", offset, " * ::capnp::POINTERS);\n"
                "}\n")
            : kj::strTree(
              "inline ", type, "::Builder ", scope, "Builder::init", titleCase, "(unsigned int size) {\n",
              unionDiscrim.set,
              "  return ::capnp::_::PointerHelpers<", type, ">::init(\n"
              "      _builder, ", offset, " * ::capnp::POINTERS, size);\n"
              "}\n"),
            "inline void ", scope, "Builder::adopt", titleCase, "(\n"
            "    ::capnp::Orphan<", type, ">&& value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::adopt(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, kj::mv(value));\n"
            "}\n"
            "inline ::capnp::Orphan<", type, "> ", scope, "Builder::disown", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::disown(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "\n")
      };
    }
  }

  // -----------------------------------------------------------------

  kj::StringTree makeReaderDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                               bool isUnion, kj::Array<kj::StringTree>&& methodDecls) {
    return kj::strTree(
        "class ", fullName, "::Reader {\n"
        "public:\n"
        "  typedef ", unqualifiedParentType, " Reads;\n"
        "\n"
        "  Reader() = default;\n"
        "  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}\n"
        "\n"
        "  inline size_t totalSizeInWords() const {\n"
        "    return _reader.totalSize() / ::capnp::WORDS;\n"
        "  }\n"
        "\n",
        isUnion ? kj::strTree("  inline Which which() const;\n") : kj::strTree(),
        kj::mv(methodDecls),
        "private:\n"
        "  ::capnp::_::StructReader _reader;\n"
        "  template <typename T, ::capnp::Kind k>\n"
        "  friend struct ::capnp::ToDynamic_;\n"
        "  template <typename T, ::capnp::Kind k>\n"
        "  friend struct ::capnp::_::PointerHelpers;\n"
        "  template <typename T, ::capnp::Kind k>\n"
        "  friend struct ::capnp::List;\n"
        "  friend class ::capnp::MessageBuilder;\n"
        "  friend class ::capnp::Orphanage;\n"
        "  friend ::kj::StringTree KJ_STRINGIFY(", fullName, "::Reader reader);\n"
        "};\n"
        "\n"
        "inline ::kj::StringTree KJ_STRINGIFY(", fullName, "::Reader reader) {\n"
        "  return ::capnp::_::structString<", fullName, ">(reader._reader);\n"
        "}\n"
        "\n");
  }

  kj::StringTree makeBuilderDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                                bool isUnion, kj::Array<kj::StringTree>&& methodDecls) {
    return kj::strTree(
        "class ", fullName, "::Builder {\n"
        "public:\n"
        "  typedef ", unqualifiedParentType, " Builds;\n"
        "\n"
        "  Builder() = delete;  // Deleted to discourage incorrect usage.\n"
        "                       // You can explicitly initialize to nullptr instead.\n"
        "  inline Builder(decltype(nullptr)) {}\n"
        "  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}\n"
        "  inline operator Reader() const { return Reader(_builder.asReader()); }\n"
        "  inline Reader asReader() const { return *this; }\n"
        "\n"
        "  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }\n"
        "\n",
        isUnion ? kj::strTree("  inline Which which();\n") : kj::strTree(),
        kj::mv(methodDecls),
        "private:\n"
        "  ::capnp::_::StructBuilder _builder;\n"
        "  template <typename T, ::capnp::Kind k>\n"
        "  friend struct ::capnp::ToDynamic_;\n"
        "  friend class ::capnp::Orphanage;\n"
        "  friend ::kj::StringTree KJ_STRINGIFY(", fullName, "::Builder builder);\n"
        "};\n"
        "\n"
        "inline ::kj::StringTree KJ_STRINGIFY(", fullName, "::Builder builder) {\n"
        "  return ::capnp::_::structString<", fullName, ">(builder._builder.asReader());\n"
        "}\n"
        "\n");
  }

  // -----------------------------------------------------------------

  struct ConstText {
    bool needsSchema;
    kj::StringTree decl;
    kj::StringTree def;
  };

  ConstText makeConstText(kj::StringPtr scope, kj::StringPtr name, ConstSchema schema) {
    auto proto = schema.getProto();
    auto constProto = proto.getConst();
    auto type = constProto.getType();
    auto typeName_ = typeName(type).flatten();
    auto upperCase = toUpperCase(name);

    // Linkage qualifier for non-primitive types.
    const char* linkage = scope.size() == 0 ? "extern " : "static ";

    switch (type.which()) {
      case schema::Value::VOID:
      case schema::Value::BOOL:
      case schema::Value::INT8:
      case schema::Value::INT16:
      case schema::Value::INT32:
      case schema::Value::INT64:
      case schema::Value::UINT8:
      case schema::Value::UINT16:
      case schema::Value::UINT32:
      case schema::Value::UINT64:
      case schema::Value::FLOAT32:
      case schema::Value::FLOAT64:
      case schema::Value::ENUM:
        return ConstText {
          false,
          kj::strTree("static constexpr ", typeName_, ' ', upperCase, " = ",
              literalValue(constProto.getType(), constProto.getValue()), ";\n"),
          scope.size() == 0 ? kj::strTree() : kj::strTree(
              "constexpr ", typeName_, ' ', scope, upperCase, ";\n")
        };

      case schema::Value::TEXT: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstText<", schema.as<Text>().size(), ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::DATA: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstData<", schema.as<Data>().size(), ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::STRUCT: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstStruct<", typeName_, ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::LIST: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstList<", typeName(type.getList().getElementType()), ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::OBJECT:
      case schema::Value::INTERFACE:
        return ConstText { false, kj::strTree(), kj::strTree() };
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  struct NodeText {
    kj::StringTree outerTypeDecl;
    kj::StringTree outerTypeDef;
    kj::StringTree readerBuilderDefs;
    kj::StringTree inlineMethodDefs;
    kj::StringTree capnpSchemaDecls;
    kj::StringTree capnpSchemaDefs;
    kj::StringTree capnpPrivateDecls;
    kj::StringTree capnpPrivateDefs;
    kj::StringTree sourceFileDefs;
  };

  NodeText makeNodeText(kj::StringPtr namespace_, kj::StringPtr scope,
                        kj::StringPtr name, Schema schema) {
    auto proto = schema.getProto();
    auto fullName = kj::str(scope, name);
    auto subScope = kj::str(fullName, "::");
    auto hexId = kj::hex(proto.getId());

    // Compute nested nodes, including groups.
    kj::Vector<NodeText> nestedTexts(proto.getNestedNodes().size());
    for (auto nested: proto.getNestedNodes()) {
      nestedTexts.add(makeNodeText(
          namespace_, subScope, nested.getName(), schemaLoader.get(nested.getId())));
    };

    if (proto.isStruct()) {
      for (auto field: proto.getStruct().getFields()) {
        if (field.isGroup()) {
          nestedTexts.add(makeNodeText(
              namespace_, subScope, toTitleCase(field.getName()),
              schemaLoader.get(field.getGroup().getTypeId())));
        }
      }
    }

    // Convert the encoded schema to a literal byte array.
    kj::ArrayPtr<const word> rawSchema = schema.asUncheckedMessage();
    auto schemaLiteral = kj::StringTree(KJ_MAP(w, rawSchema) {
      const byte* bytes = reinterpret_cast<const byte*>(&w);

      return kj::strTree(KJ_MAP(i, kj::range<uint>(0, sizeof(word))) {
        auto text = kj::toCharSequence(kj::implicitCast<uint>(bytes[i]));
        return kj::strTree(kj::repeat(' ', 4 - text.size()), text, ",");
      });
    }, "\n   ");

    auto schemaDecl = kj::strTree(
        "extern const ::capnp::_::RawSchema s_", hexId, ";\n");

    std::set<uint64_t> deps;
    enumerateDeps(proto, deps);

    kj::Array<uint> membersByName;
    kj::Array<uint> membersByDiscrim;
    switch (proto.which()) {
      case schema::Node::STRUCT: {
        auto structSchema = schema.asStruct();
        membersByName = makeMembersByName(structSchema.getFields());
        auto builder = kj::heapArrayBuilder<uint>(structSchema.getFields().size());
        for (auto field: structSchema.getUnionFields()) {
          builder.add(field.getIndex());
        }
        for (auto field: structSchema.getNonUnionFields()) {
          builder.add(field.getIndex());
        }
        membersByDiscrim = builder.finish();
        break;
      }
      case schema::Node::ENUM:
        membersByName = makeMembersByName(schema.asEnum().getEnumerants());
        break;
      case schema::Node::INTERFACE:
        membersByName = makeMembersByName(schema.asInterface().getMethods());
        break;
      default:
        break;
    }

    auto schemaDef = kj::strTree(
        "static const ::capnp::_::AlignedData<", rawSchema.size(), "> b_", hexId, " = {\n"
        "  {", kj::mv(schemaLiteral), " }\n"
        "};\n",
        deps.size() == 0 ? kj::strTree() : kj::strTree(
            "static const ::capnp::_::RawSchema* const d_", hexId, "[] = {\n",
            KJ_MAP(depId, deps) {
              return kj::strTree("  &s_", kj::hex(depId), ",\n");
            },
            "};\n"),
        membersByName.size() == 0 ? kj::strTree() : kj::strTree(
            "static const uint16_t m_", hexId, "[] = {",
            kj::StringTree(KJ_MAP(index, membersByName) { return kj::strTree(index); }, ", "),
            "};\n"),
        membersByDiscrim.size() == 0 ? kj::strTree() : kj::strTree(
            "static const uint16_t i_", hexId, "[] = {",
            kj::StringTree(KJ_MAP(index, membersByDiscrim) { return kj::strTree(index); }, ", "),
            "};\n"),
        "const ::capnp::_::RawSchema s_", hexId, " = {\n"
        "  0x", hexId, ", b_", hexId, ".words, ", rawSchema.size(), ", ",
        deps.size() == 0 ? kj::strTree("nullptr") : kj::strTree("d_", hexId), ", ",
        membersByName.size() == 0 ? kj::strTree("nullptr") : kj::strTree("m_", hexId), ",\n",
        "  ", deps.size(), ", ", membersByName.size(), ", ",
        membersByDiscrim.size() == 0 ? kj::strTree("nullptr") : kj::strTree("i_", hexId),
        ", nullptr, nullptr\n"
        "};\n");

    switch (proto.which()) {
      case schema::Node::FILE:
        KJ_FAIL_REQUIRE("This method shouldn't be called on file nodes.");

      case schema::Node::STRUCT: {
        auto fieldTexts =
            KJ_MAP(f, schema.asStruct().getFields()) { return makeFieldText(subScope, f); };

        auto structNode = proto.getStruct();
        uint discrimOffset = structNode.getDiscriminantOffset();

        return NodeText {
          kj::strTree(
              "  struct ", name, ";\n"),

          kj::strTree(
              "struct ", fullName, " {\n",
              "  ", name, "() = delete;\n"
              "\n"
              "  class Reader;\n"
              "  class Builder;\n",
              structNode.getDiscriminantCount() == 0 ? kj::strTree() : kj::strTree(
                  "  enum Which: uint16_t {\n",
                  KJ_MAP(f, structNode.getFields()) {
                    if (f.hasDiscriminantValue()) {
                      return kj::strTree("    ", toUpperCase(f.getName()), ",\n");
                    } else {
                      return kj::strTree();
                    }
                  },
                  "  };\n"),
              KJ_MAP(n, nestedTexts) { return kj::mv(n.outerTypeDecl); },
              "};\n"
              "\n",
              KJ_MAP(n, nestedTexts) { return kj::mv(n.outerTypeDef); }),

          kj::strTree(
              makeReaderDef(fullName, name, structNode.getDiscriminantCount() != 0,
                            KJ_MAP(f, fieldTexts) { return kj::mv(f.readerMethodDecls); }),
              makeBuilderDef(fullName, name, structNode.getDiscriminantCount() != 0,
                             KJ_MAP(f, fieldTexts) { return kj::mv(f.builderMethodDecls); }),
              KJ_MAP(n, nestedTexts) { return kj::mv(n.readerBuilderDefs); }),

          kj::strTree(
              structNode.getDiscriminantCount() == 0 ? kj::strTree() : kj::strTree(
                  "inline ", fullName, "::Which ", fullName, "::Reader::which() const {\n"
                  "  return _reader.getDataField<Which>(", discrimOffset, " * ::capnp::ELEMENTS);\n"
                  "}\n"
                  "inline ", fullName, "::Which ", fullName, "::Builder::which() {\n"
                  "  return _builder.getDataField<Which>(", discrimOffset, " * ::capnp::ELEMENTS);\n"
                  "}\n"
                  "\n"),
              KJ_MAP(f, fieldTexts) { return kj::mv(f.inlineMethodDefs); },
              KJ_MAP(n, nestedTexts) { return kj::mv(n.inlineMethodDefs); }),

          kj::strTree(
              kj::mv(schemaDecl),
              KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpSchemaDecls); }),

          kj::strTree(
              kj::mv(schemaDef),
              KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpSchemaDefs); }),

          kj::strTree(
              "CAPNP_DECLARE_STRUCT(\n"
              "    ", namespace_, "::", fullName, ", ", hexId, ",\n"
              "    ", structNode.getDataWordCount(), ", ",
                      structNode.getPointerCount(), ", ",
                      FIELD_SIZE_NAMES[static_cast<uint>(structNode.getPreferredListEncoding())],
                      ");\n",
              KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpPrivateDecls); }),

          kj::strTree(
              "CAPNP_DEFINE_STRUCT(\n"
              "    ", namespace_, "::", fullName, ");\n",
              KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpPrivateDefs); }),

          kj::strTree(KJ_MAP(n, nestedTexts) { return kj::mv(n.sourceFileDefs); }),
        };
      }

      case schema::Node::ENUM: {
        auto enumerants = schema.asEnum().getEnumerants();

        return NodeText {
          scope.size() == 0 ? kj::strTree() : kj::strTree(
              "  enum class ", name, ": uint16_t {\n",
              KJ_MAP(e, enumerants) {
                return kj::strTree("    ", toUpperCase(e.getProto().getName()), ",\n");
              },
              "  };\n"
              "\n"),

          scope.size() > 0 ? kj::strTree() : kj::strTree(
              "enum class ", name, ": uint16_t {\n",
              KJ_MAP(e, enumerants) {
                return kj::strTree("  ", toUpperCase(e.getProto().getName()), ",\n");
              },
              "};\n"
              "\n"),

          kj::strTree(),
          kj::strTree(),

          kj::mv(schemaDecl),
          kj::mv(schemaDef),

          kj::strTree(
              "CAPNP_DECLARE_ENUM(\n"
              "    ", namespace_, "::", fullName, ", ", hexId, ");\n"),
          kj::strTree(
              "CAPNP_DEFINE_ENUM(\n"
              "    ", namespace_, "::", fullName, ");\n"),

          kj::strTree(),
        };
      }

      case schema::Node::INTERFACE: {
        return NodeText {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),

          kj::mv(schemaDecl),
          kj::mv(schemaDef),

          kj::strTree(
              "CAPNP_DECLARE_INTERFACE(\n"
              "    ", namespace_, "::", fullName, ", ", hexId, ");\n"),
          kj::strTree(
              "CAPNP_DEFINE_INTERFACE(\n"
              "    ", namespace_, "::", fullName, ");\n"),

          kj::strTree(),
        };
      }

      case schema::Node::CONST: {
        auto constText = makeConstText(scope, name, schema.asConst());

        return NodeText {
          scope.size() == 0 ? kj::strTree() : kj::strTree("  ", kj::mv(constText.decl)),
          scope.size() > 0 ? kj::strTree() : kj::mv(constText.decl),
          kj::strTree(),
          kj::strTree(),

          constText.needsSchema ? kj::mv(schemaDecl) : kj::strTree(),
          constText.needsSchema ? kj::mv(schemaDef) : kj::strTree(),

          kj::strTree(),
          kj::strTree(),

          kj::mv(constText.def),
        };
      }

      case schema::Node::ANNOTATION: {
        return NodeText {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),

          kj::mv(schemaDecl),
          kj::mv(schemaDef),

          kj::strTree(),
          kj::strTree(),

          kj::strTree(),
        };
      }
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  struct FileText {
    kj::StringTree header;
    kj::StringTree source;
  };

  FileText makeFileText(Schema schema,
                        schema::CodeGeneratorRequest::RequestedFile::Reader request) {
    usedImports.clear();

    auto node = schema.getProto();
    auto displayName = node.getDisplayName();

    kj::Vector<kj::ArrayPtr<const char>> namespaceParts;
    kj::String namespacePrefix;

    for (auto annotation: node.getAnnotations()) {
      if (annotation.getId() == NAMESPACE_ANNOTATION_ID) {
        kj::StringPtr ns = annotation.getValue().getText();
        kj::StringPtr ns2 = ns;
        namespacePrefix = kj::str("::", ns);

        for (;;) {
          KJ_IF_MAYBE(colonPos, ns.findFirst(':')) {
            namespaceParts.add(ns.slice(0, *colonPos));
            ns = ns.slice(*colonPos);
            if (!ns.startsWith("::")) {
              context.exitError(kj::str(displayName, ": invalid namespace spec: ", ns2));
            }
            ns = ns.slice(2);
          } else {
            namespaceParts.add(ns);
            break;
          }
        }

        break;
      }
    }

    auto nodeTexts = KJ_MAP(nested, node.getNestedNodes()) {
      return makeNodeText(namespacePrefix, "", nested.getName(), schemaLoader.get(nested.getId()));
    };

    kj::String separator = kj::str("// ", kj::repeat('=', 87), "\n");

    kj::Vector<kj::StringPtr> includes;
    for (auto import: request.getImports()) {
      if (usedImports.count(import.getId()) > 0) {
        includes.add(import.getName());
      }
    }

    kj::StringTree sourceDefs = kj::strTree(
        KJ_MAP(n, nodeTexts) { return kj::mv(n.sourceFileDefs); });

    return FileText {
      kj::strTree(
          "// Generated by Cap'n Proto compiler, DO NOT EDIT\n"
          "// source: ", baseName(displayName), "\n"
          "\n"
          "#ifndef CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n",
          "#define CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n"
          "\n"
          "#include <capnp/generated-header-support.h>\n"
          "\n"
          "#if CAPNP_VERSION != ", CAPNP_VERSION, "\n"
          "#error \"Version mismatch between generated code and library headers.  You must "
              "use the same version of the Cap'n Proto compiler and library.\"\n"
          "#endif\n"
          "\n",
          KJ_MAP(path, includes) {
            if (path.startsWith("/")) {
              return kj::strTree("#include <", path.slice(1), ".h>\n");
            } else {
              return kj::strTree("#include \"", path, ".h\"\n");
            }
          },
          "\n",

          KJ_MAP(n, namespaceParts) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.outerTypeDef); },
          KJ_MAP(n, namespaceParts) { return kj::strTree("}  // namespace\n"); }, "\n",

          separator, "\n"
          "namespace capnp {\n"
          "namespace schemas {\n"
          "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpSchemaDecls); },
          "\n"
          "}  // namespace schemas\n"
          "namespace _ {  // private\n"
          "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpPrivateDecls); },
          "\n"
          "}  // namespace _ (private)\n"
          "}  // namespace capnp\n"

          "\n", separator, "\n",
          KJ_MAP(n, namespaceParts) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.readerBuilderDefs); },
          separator, "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.inlineMethodDefs); },
          KJ_MAP(n, namespaceParts) { return kj::strTree("}  // namespace\n"); }, "\n",
          "#endif  // CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n"),

      kj::strTree(
          "// Generated by Cap'n Proto compiler, DO NOT EDIT\n"
          "// source: ", baseName(displayName), "\n"
          "\n"
          "#include \"", baseName(displayName), ".h\"\n"
          "\n"
          "namespace capnp {\n"
          "namespace schemas {\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpSchemaDefs); },
          "}  // namespace schemas\n"
          "namespace _ {  // private\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpPrivateDefs); },
          "}  // namespace _ (private)\n"
          "}  // namespace capnp\n",
          sourceDefs.size() == 0 ? kj::strTree() : kj::strTree(
              "\n", separator, "\n",
              KJ_MAP(n, namespaceParts) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
              kj::mv(sourceDefs), "\n",
              KJ_MAP(n, namespaceParts) { return kj::strTree("}  // namespace\n"); }, "\n"))
    };
  }

  // -----------------------------------------------------------------

  void makeDirectory(kj::StringPtr path) {
    KJ_IF_MAYBE(slashpos, path.findLast('/')) {
      // Make the parent dir.
      makeDirectory(kj::str(path.slice(0, *slashpos)));
    }

    if (mkdir(path.cStr(), 0777) < 0) {
      int error = errno;
      if (error != EEXIST) {
        KJ_FAIL_SYSCALL("mkdir(path)", error, path);
      }
    }
  }

  void writeFile(kj::StringPtr filename, const kj::StringTree& text) {
    KJ_IF_MAYBE(slashpos, filename.findLast('/')) {
      // Make the parent dir.
      makeDirectory(kj::str(filename.slice(0, *slashpos)));
    }

    int fd;
    KJ_SYSCALL(fd = open(filename.cStr(), O_CREAT | O_WRONLY | O_TRUNC, 0666), filename);
    kj::FdOutputStream out((kj::AutoCloseFd(fd)));

    text.visit(
        [&](kj::ArrayPtr<const char> text) {
          out.write(text.begin(), text.size());
        });
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
      auto schema = schemaLoader.get(requestedFile.getId());
      auto fileText = makeFileText(schema, requestedFile);

      writeFile(kj::str(schema.getProto().getDisplayName(), ".h"), fileText.header);
      writeFile(kj::str(schema.getProto().getDisplayName(), ".c++"), fileText.source);
    }

    return true;
  }
};

}  // namespace
}  // namespace capnp

KJ_MAIN(capnp::CapnpcCppMain);
