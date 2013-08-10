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
  switch (type.getBody().which()) {
    case schema::Type::Body::STRUCT_TYPE:
      deps.insert(type.getBody().getStructType());
      break;
    case schema::Type::Body::ENUM_TYPE:
      deps.insert(type.getBody().getEnumType());
      break;
    case schema::Type::Body::INTERFACE_TYPE:
      deps.insert(type.getBody().getInterfaceType());
      break;
    case schema::Type::Body::LIST_TYPE:
      enumerateDeps(type.getBody().getListType(), deps);
      break;
    default:
      break;
  }
}

void enumerateDeps(schema::StructNode::Member::Reader member, std::set<uint64_t>& deps) {
  switch (member.getBody().which()) {
    case schema::StructNode::Member::Body::FIELD_MEMBER:
      enumerateDeps(member.getBody().getFieldMember().getType(), deps);
      break;
    case schema::StructNode::Member::Body::UNION_MEMBER:
      for (auto subMember: member.getBody().getUnionMember().getMembers()) {
        enumerateDeps(subMember, deps);
      }
      break;
    case schema::StructNode::Member::Body::GROUP_MEMBER:
      for (auto subMember: member.getBody().getGroupMember().getMembers()) {
        enumerateDeps(subMember, deps);
      }
      break;
  }
}

void enumerateDeps(schema::Node::Reader node, std::set<uint64_t>& deps) {
  switch (node.getBody().which()) {
    case schema::Node::Body::STRUCT_NODE:
      for (auto member: node.getBody().getStructNode().getMembers()) {
        enumerateDeps(member, deps);
      }
      break;
    case schema::Node::Body::INTERFACE_NODE:
      for (auto method: node.getBody().getInterfaceNode().getMethods()) {
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
void makeMemberInfoTable(uint parent, MemberList&& members,
                         kj::Vector<capnp::_::RawSchema::MemberInfo>& info);

void makeSubMemberInfoTable(const StructSchema::Member& member,
                            kj::Vector<capnp::_::RawSchema::MemberInfo>& info) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::FIELD_MEMBER:
      break;
    case schema::StructNode::Member::Body::UNION_MEMBER:
      makeMemberInfoTable(1 + member.getIndex(), member.asUnion().getMembers(), info);
      break;
    case schema::StructNode::Member::Body::GROUP_MEMBER:
      makeMemberInfoTable(1 + member.getIndex(), member.asGroup().getMembers(), info);
      break;
  }
}
void makeSubMemberInfoTable(const EnumSchema::Enumerant& member,
                            kj::Vector<capnp::_::RawSchema::MemberInfo>& info) {}
void makeSubMemberInfoTable(const InterfaceSchema::Method& member,
                            kj::Vector<capnp::_::RawSchema::MemberInfo>& info) {}

template <typename MemberList>
void makeMemberInfoTable(uint parent, MemberList&& members,
                         kj::Vector<capnp::_::RawSchema::MemberInfo>& info) {
  auto sorted = KJ_MAP(members, m) { return m; };
  std::sort(sorted.begin(), sorted.end(), OrderByName());

  for (auto& member: sorted) {
    info.add(capnp::_::RawSchema::MemberInfo {
      kj::implicitCast<uint16_t>(parent),
      kj::implicitCast<uint16_t>(member.getIndex())
    });
  }
  for (auto member: members) {
    makeSubMemberInfoTable(member, info);
  }
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
          return kj::strTree(" ::", annotation.getValue().getBody().getTextValue());
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
    switch (type.getBody().which()) {
      case schema::Type::Body::VOID_TYPE: return kj::strTree(" ::capnp::Void");

      case schema::Type::Body::BOOL_TYPE: return kj::strTree("bool");
      case schema::Type::Body::INT8_TYPE: return kj::strTree(" ::int8_t");
      case schema::Type::Body::INT16_TYPE: return kj::strTree(" ::int16_t");
      case schema::Type::Body::INT32_TYPE: return kj::strTree(" ::int32_t");
      case schema::Type::Body::INT64_TYPE: return kj::strTree(" ::int64_t");
      case schema::Type::Body::UINT8_TYPE: return kj::strTree(" ::uint8_t");
      case schema::Type::Body::UINT16_TYPE: return kj::strTree(" ::uint16_t");
      case schema::Type::Body::UINT32_TYPE: return kj::strTree(" ::uint32_t");
      case schema::Type::Body::UINT64_TYPE: return kj::strTree(" ::uint64_t");
      case schema::Type::Body::FLOAT32_TYPE: return kj::strTree("float");
      case schema::Type::Body::FLOAT64_TYPE: return kj::strTree("double");

      case schema::Type::Body::TEXT_TYPE: return kj::strTree(" ::capnp::Text");
      case schema::Type::Body::DATA_TYPE: return kj::strTree(" ::capnp::Data");

      case schema::Type::Body::ENUM_TYPE:
        return cppFullName(schemaLoader.get(type.getBody().getEnumType()));
      case schema::Type::Body::STRUCT_TYPE:
        return cppFullName(schemaLoader.get(type.getBody().getStructType()));
      case schema::Type::Body::INTERFACE_TYPE:
        return cppFullName(schemaLoader.get(type.getBody().getInterfaceType()));

      case schema::Type::Body::LIST_TYPE:
        return kj::strTree(" ::capnp::List<", typeName(type.getBody().getListType()), ">");

      case schema::Type::Body::OBJECT_TYPE:
        // Not used.
        return kj::strTree();
    }
    KJ_UNREACHABLE;
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

  FieldText makeFieldText(kj::StringPtr scope, StructSchema::Field member) {
    auto proto = member.getProto();
    auto field = proto.getBody().getFieldMember();

    FieldKind kind;
    kj::String ownedType;
    kj::String type = typeName(field.getType()).flatten();
    kj::StringPtr setterDefault;  // only for void
    kj::String defaultMask;    // primitives only
    size_t defaultOffset = 0;    // pointers only: offset of the default value within the schema.
    size_t defaultSize = 0;      // blobs only: byte size of the default value.

    auto typeBody = field.getType().getBody();
    auto defaultBody = field.getDefaultValue().getBody();
    switch (typeBody.which()) {
      case schema::Type::Body::VOID_TYPE:
        kind = FieldKind::PRIMITIVE;
        setterDefault = " = ::capnp::Void::VOID";
        break;

#define HANDLE_PRIMITIVE(discrim, typeName, defaultName, suffix) \
      case schema::Type::Body::discrim##_TYPE: \
        kind = FieldKind::PRIMITIVE; \
        if (defaultBody.get##defaultName##Value() != 0) { \
          defaultMask = kj::str(defaultBody.get##defaultName##Value(), #suffix); \
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

      case schema::Type::Body::FLOAT32_TYPE:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat32Value() != 0) {
          uint32_t mask;
          float value = defaultBody.getFloat32Value();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "u");
        }
        break;

      case schema::Type::Body::FLOAT64_TYPE:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat64Value() != 0) {
          uint64_t mask;
          double value = defaultBody.getFloat64Value();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "ull");
        }
        break;

      case schema::Type::Body::TEXT_TYPE:
        kind = FieldKind::BLOB;
        if (defaultBody.hasTextValue()) {
          defaultOffset = member.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getTextValue().size();
        }
        break;
      case schema::Type::Body::DATA_TYPE:
        kind = FieldKind::BLOB;
        if (defaultBody.hasDataValue()) {
          defaultOffset = member.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getDataValue().size();
        }
        break;

      case schema::Type::Body::ENUM_TYPE:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getEnumValue() != 0) {
          defaultMask = kj::str(defaultBody.getEnumValue(), "u");
        }
        break;

      case schema::Type::Body::STRUCT_TYPE:
        kind = FieldKind::STRUCT;
        if (defaultBody.hasStructValue()) {
          defaultOffset = member.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::Body::LIST_TYPE:
        kind = FieldKind::LIST;
        if (defaultBody.hasListValue()) {
          defaultOffset = member.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::Body::INTERFACE_TYPE:
        kind = FieldKind::INTERFACE;
        break;
      case schema::Type::Body::OBJECT_TYPE:
        kind = FieldKind::OBJECT;
        if (defaultBody.hasObjectValue()) {
          defaultOffset = member.getDefaultValueSchemaOffset();
        }
        break;
    }

    kj::String defaultMaskParam;
    if (defaultMask.size() > 0) {
      defaultMaskParam = kj::str(", ", defaultMask);
    }

    kj::String titleCase = toTitleCase(proto.getName());

    kj::String unionSet, unionCheck;
    KJ_IF_MAYBE(u, member.getContainingUnion()) {
      auto unionProto = u->getProto();
      kj::String unionTitleCase = toTitleCase(unionProto.getName());
      auto discrimOffset = unionProto.getBody().getUnionMember().getDiscriminantOffset();

      kj::String upperCase = toUpperCase(proto.getName());
      unionCheck = kj::str(
          "  KJ_IREQUIRE(which() == ", unionTitleCase, "::", upperCase, ",\n"
          "              \"Must check which() before get()ing a union member.\");\n");
      unionSet = kj::str(
          "  _builder.setDataField<", unionTitleCase, "::Which>(\n"
          "      ", discrimOffset, " * ::capnp::ELEMENTS, ",
                    unionTitleCase, "::", upperCase, ");\n");
    }

    uint offset = field.getOffset();

    if (kind == FieldKind::PRIMITIVE) {
      return FieldText {
        kj::strTree(
            "  inline ", type, " get", titleCase, "() const;\n"
            "\n"),

        kj::strTree(
            "  inline ", type, " get", titleCase, "();\n"
            "  inline void set", titleCase, "(", type, " value", setterDefault, ");\n"
            "\n"),

        kj::strTree(
            "inline ", type, " ", scope, "Reader::get", titleCase, "() const {\n",
            unionCheck,
            "  return _reader.getDataField<", type, ">(\n"
            "      ", offset, " * ::capnp::ELEMENTS", defaultMaskParam, ");\n",
            "}\n"
            "\n"
            "inline ", type, " ", scope, "Builder::get", titleCase, "() {\n",
            unionCheck,
            "  return _builder.getDataField<", type, ">(\n"
            "      ", offset, " * ::capnp::ELEMENTS", defaultMaskParam, ");\n",
            "}\n"
            "inline void ", scope, "Builder::set", titleCase, "(", type, " value) {\n",
            unionSet,
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
            "  inline bool has", titleCase, "() const;\n"
            "  template <typename T>\n"
            "  inline typename T::Reader get", titleCase, "() const;\n"
            "  template <typename T, typename Param>\n"
            "  inline typename T::Reader get", titleCase, "(Param&& param) const;\n"
            "\n"),

        kj::strTree(
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
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionCheck,
            "  return !_reader.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionCheck,
            "  return !_builder.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "template <typename T>\n"
            "inline typename T::Reader ", scope, "Reader::get", titleCase, "() const {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<T>::get(\n"
            "      _reader, ", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "template <typename T>\n"
            "inline typename T::Builder ", scope, "Builder::get", titleCase, "() {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<T>::get(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "template <typename T, typename Param>\n"
            "inline typename T::Reader ", scope, "Reader::get", titleCase, "(Param&& param) const {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<T>::getDynamic(\n"
            "      _reader, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Param>(param));\n"
            "}\n"
            "template <typename T, typename Param>\n"
            "inline typename T::Builder ", scope, "Builder::get", titleCase, "(Param&& param) {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<T>::getDynamic(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Param>(param));\n"
            "}\n"
            "template <typename T>\n"
            "inline void ", scope, "Builder::set", titleCase, "(typename T::Reader value) {\n",
            unionSet,
            "  ::capnp::_::PointerHelpers<T>::set(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
            "}\n"
            "template <typename T, typename U>"
            "inline void ", scope, "Builder::set", titleCase, "(std::initializer_list<U> value) {\n",
            unionSet,
            "  ::capnp::_::PointerHelpers<T>::set(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
            "}\n"
            "template <typename T, typename... Params>\n"
            "inline typename T::Builder ", scope, "Builder::init", titleCase, "(Params&&... params) {\n",
            unionSet,
            "  return ::capnp::_::PointerHelpers<T>::init(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);\n"
            "}\n"
            "template <typename T>\n"
            "inline void ", scope, "Builder::adopt", titleCase, "(::capnp::Orphan<T>&& value) {\n",
            unionSet,
            "  ::capnp::_::PointerHelpers<T>::adopt(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, kj::mv(value));\n"
            "}\n"
            "template <typename T, typename... Params>\n"
            "inline ::capnp::Orphan<T> ", scope, "Builder::disown", titleCase, "(Params&&... params) {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<T>::disown(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);\n"
            "}\n"
            "\n")
      };

    } else {
      // Blob, struct, or list.  These have only minor differences.

      uint64_t typeId = member.getContainingStruct().getProto().getId();
      kj::String defaultParam = defaultOffset == 0 ? kj::str() : kj::str(
          ",\n        ::capnp::schemas::s_", kj::hex(typeId), ".encodedNode + ", defaultOffset,
          defaultSize == 0 ? kj::strTree() : kj::strTree(", ", defaultSize));

      kj::String elementReaderType;
      bool isStructList = false;
      if (kind == FieldKind::LIST) {
        bool primitiveElement = false;
        switch (typeBody.getListType().getBody().which()) {
          case schema::Type::Body::VOID_TYPE:
          case schema::Type::Body::BOOL_TYPE:
          case schema::Type::Body::INT8_TYPE:
          case schema::Type::Body::INT16_TYPE:
          case schema::Type::Body::INT32_TYPE:
          case schema::Type::Body::INT64_TYPE:
          case schema::Type::Body::UINT8_TYPE:
          case schema::Type::Body::UINT16_TYPE:
          case schema::Type::Body::UINT32_TYPE:
          case schema::Type::Body::UINT64_TYPE:
          case schema::Type::Body::FLOAT32_TYPE:
          case schema::Type::Body::FLOAT64_TYPE:
          case schema::Type::Body::ENUM_TYPE:
            primitiveElement = true;
            break;

          case schema::Type::Body::TEXT_TYPE:
          case schema::Type::Body::DATA_TYPE:
          case schema::Type::Body::LIST_TYPE:
          case schema::Type::Body::INTERFACE_TYPE:
          case schema::Type::Body::OBJECT_TYPE:
            primitiveElement = false;
            break;

          case schema::Type::Body::STRUCT_TYPE:
            isStructList = true;
            primitiveElement = false;
            break;
        }
        elementReaderType = kj::str(
            typeName(typeBody.getListType()),
            primitiveElement ? "" : "::Reader");
      }

      return FieldText {
        kj::strTree(
            "  inline bool has", titleCase, "() const;\n"
            "  inline ", type, "::Reader get", titleCase, "() const;\n"
            "\n"),

        kj::strTree(
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
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionCheck,
            "  return !_reader.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionCheck,
            "  return !_builder.isPointerFieldNull(", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "inline ", type, "::Reader ", scope, "Reader::get", titleCase, "() const {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(\n"
            "      _reader, ", offset, " * ::capnp::POINTERS", defaultParam, ");\n"
            "}\n"
            "inline ", type, "::Builder ", scope, "Builder::get", titleCase, "() {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS", defaultParam, ");\n"
            "}\n"
            "inline void ", scope, "Builder::set", titleCase, "(", type, "::Reader value) {\n",
            unionSet,
            "  ::capnp::_::PointerHelpers<", type, ">::set(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
            "}\n",
            kind == FieldKind::LIST && !isStructList
            ? kj::strTree(
              "inline void ", scope, "Builder::set", titleCase, "(std::initializer_list<", elementReaderType, "> value) {\n",
              unionSet,
              "  ::capnp::_::PointerHelpers<", type, ">::set(\n"
              "      _builder, ", offset, " * ::capnp::POINTERS, value);\n"
              "}\n")
            : kj::strTree(),
            kind == FieldKind::STRUCT
            ? kj::strTree(
                "inline ", type, "::Builder ", scope, "Builder::init", titleCase, "() {\n",
                unionSet,
                "  return ::capnp::_::PointerHelpers<", type, ">::init(\n"
                "      _builder, ", offset, " * ::capnp::POINTERS);\n"
                "}\n")
            : kj::strTree(
              "inline ", type, "::Builder ", scope, "Builder::init", titleCase, "(unsigned int size) {\n",
              unionSet,
              "  return ::capnp::_::PointerHelpers<", type, ">::init(\n"
              "      _builder, ", offset, " * ::capnp::POINTERS, size);\n"
              "}\n"),
            "inline void ", scope, "Builder::adopt", titleCase, "(\n"
            "    ::capnp::Orphan<", type, ">&& value) {\n",
            unionSet,
            "  ::capnp::_::PointerHelpers<", type, ">::adopt(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS, kj::mv(value));\n"
            "}\n"
            "inline ::capnp::Orphan<", type, "> ", scope, "Builder::disown", titleCase, "() {\n",
            unionCheck,
            "  return ::capnp::_::PointerHelpers<", type, ">::disown(\n"
            "      _builder, ", offset, " * ::capnp::POINTERS);\n"
            "}\n"
            "\n")
      };
    }
  }

  // -----------------------------------------------------------------

  kj::StringTree makeReaderDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                               kj::StringPtr stringifier, kj::StringTree&& methodDecls) {
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
        "  return ::capnp::_::", stringifier, "<", fullName, ">(reader._reader);\n"
        "}\n"
        "\n");
  }

  kj::StringTree makeBuilderDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                                kj::StringPtr stringifier, kj::StringTree&& methodDecls) {
    return kj::strTree(
        "class ", fullName, "::Builder {\n"
        "public:\n"
        "  typedef ", unqualifiedParentType, " Builds;\n"
        "\n"
        "  Builder() = default;\n"
        "  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}\n"
        "  inline operator Reader() const { return Reader(_builder.asReader()); }\n"
        "  inline Reader asReader() const { return *this; }\n"
        "\n"
        "  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }\n"
        "\n",
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
        "  return ::capnp::_::", stringifier, "<", fullName, ">(builder._builder.asReader());\n"
        "}\n"
        "\n");
  }

  // -----------------------------------------------------------------

  struct MembersText {
    kj::StringTree innerTypeDecls;
    kj::StringTree innerTypeDefs;
    kj::StringTree innerTypeReaderBuilderDefs;
    kj::StringTree readerMethodDecls;
    kj::StringTree builderMethodDecls;
    kj::StringTree inlineMethodDefs;
    kj::StringTree capnpPrivateDecls;
    kj::StringTree capnpPrivateDefs;
  };

  MembersText makeMemberText(kj::StringPtr namespace_, kj::StringPtr containingType,
                             StructSchema::Member member) {
    auto proto = member.getProto();
    switch (proto.getBody().which()) {
      case schema::StructNode::Member::Body::FIELD_MEMBER: {
        auto fieldText = makeFieldText(kj::str(containingType, "::"), member.asField());
        return MembersText {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::mv(fieldText.readerMethodDecls),
          kj::mv(fieldText.builderMethodDecls),
          kj::mv(fieldText.inlineMethodDefs),
          kj::strTree(),
          kj::strTree(),
        };
      }

      case schema::StructNode::Member::Body::UNION_MEMBER: {
        auto subMembers = member.asUnion().getMembers();

        auto unionName = proto.getName();

        uint discrimOffset = proto.getBody().getUnionMember().getDiscriminantOffset();

        auto whichEnumDef = kj::strTree(
            "  enum Which: uint16_t {\n",
            KJ_MAP(subMembers, subMember) {
              return kj::strTree(
                  "    ", toUpperCase(subMember.getProto().getName()), ",\n");
            },
            "  };\n");

        if (unionName.size() == 0) {
          // Anonymous union.
          auto subText = makeMembersText(namespace_, containingType, subMembers);

          return MembersText {
            kj::strTree(kj::mv(whichEnumDef), kj::mv(subText.innerTypeDecls)),
            kj::mv(subText.innerTypeDefs),
            kj::mv(subText.innerTypeReaderBuilderDefs),

            kj::strTree(
                "inline Which which() const;\n",
                kj::mv(subText.readerMethodDecls)),
            kj::strTree(
                "inline Which which();\n",
                kj::mv(subText.builderMethodDecls)),
            kj::strTree(
                "inline ", containingType, "::Which ", containingType, "::Reader::which() const {\n"
                "  return _reader.getDataField<Which>(", discrimOffset, " * ::capnp::ELEMENTS);\n"
                "}\n"
                "inline ", containingType, "::Which ", containingType, "::Builder::which() {\n"
                "  return _builder.getDataField<Which>(", discrimOffset, " * ::capnp::ELEMENTS);\n"
                "}\n"
                "\n",
                kj::mv(subText.inlineMethodDefs)),
            kj::mv(subText.capnpPrivateDecls),
            kj::mv(subText.capnpPrivateDefs),
          };
        } else {
          // Named union.
          auto titleCase = toTitleCase(unionName);
          auto fullName = kj::str(containingType, "::", titleCase);
          auto subText = makeMembersText(namespace_, fullName, subMembers);

          return MembersText {
            kj::strTree(
                "  struct ", titleCase, ";\n"),

            kj::strTree(
                "struct ", fullName, " {\n"
                "  ", titleCase, "() = delete;\n"
                "  class Reader;\n"
                "  class Builder;\n"
                "\n",
                kj::mv(whichEnumDef),
                kj::mv(subText.innerTypeDecls),
                "};\n"
                "\n",
                kj::mv(subText.innerTypeDefs)),

            kj::strTree(
                makeReaderDef(fullName, titleCase, "unionString", kj::strTree(
                    "inline Which which() const;\n",
                    kj::mv(subText.readerMethodDecls))),
                makeBuilderDef(fullName, titleCase, "unionString", kj::strTree(
                    "inline Which which();\n",
                    kj::mv(subText.builderMethodDecls)))),

            kj::strTree(
                "inline ", titleCase, "::Reader get", titleCase, "() const;\n"),

            kj::strTree(
                "inline ", titleCase, "::Builder get", titleCase, "();\n"),

            kj::strTree(
                "inline ", fullName, "::Reader ", containingType, "::Reader::get", titleCase, "() const {\n"
                "  return ", fullName, "::Reader(_reader);\n"
                "}\n"
                "inline ", fullName, "::Builder ", containingType, "::Builder::get", titleCase, "() {\n"
                "  return ", fullName, "::Builder(_builder);\n"
                "}\n"
                "inline ", fullName, "::Which ", fullName, "::Reader::which() const {\n"
                "  return _reader.getDataField<Which>(", discrimOffset, " * ::capnp::ELEMENTS);\n"
                "}\n"
                "inline ", fullName, "::Which ", fullName, "::Builder::which() {\n"
                "  return _builder.getDataField<Which>(", discrimOffset, " * ::capnp::ELEMENTS);\n"
                "}\n"
                "\n",
                kj::mv(subText.inlineMethodDefs)),

            kj::strTree(
                "CAPNP_DECLARE_UNION(\n"
                "    ", namespace_, "::", fullName, ",\n"
                "    ", namespace_, "::", containingType, ", ", member.getIndex(), ");\n",
                kj::mv(subText.capnpPrivateDecls)),
            kj::strTree(
                "CAPNP_DEFINE_UNION(\n"
                "    ", namespace_, "::", fullName, ");\n",
                kj::mv(subText.capnpPrivateDefs)),
          };
        }
      }

      case schema::StructNode::Member::Body::GROUP_MEMBER: {
        auto titleCase = toTitleCase(proto.getName());
        auto fullName = kj::str(containingType, "::", titleCase);
        auto subText = makeMembersText(namespace_, fullName, member.asGroup().getMembers());

        return MembersText {
          kj::strTree(
              "  struct ", titleCase, ";\n"),

          kj::strTree(
              "struct ", containingType, "::", titleCase, " {\n"
              "  ", titleCase, "() = delete;\n"
              "\n",
              "  class Reader;\n"
              "  class Builder;\n",
              kj::mv(subText.innerTypeDecls),
              "}\n"
              "\n",
              kj::mv(subText.innerTypeDefs)),

          kj::strTree(
              makeReaderDef(fullName, titleCase, "groupString",
                            kj::mv(subText.readerMethodDecls)),
              makeBuilderDef(fullName, titleCase, "groupString",
                             kj::mv(subText.builderMethodDecls))),

          kj::strTree(
              "inline ", titleCase, "::Reader get", titleCase, "() const;\n"),

          kj::strTree(
              "inline ", titleCase, "::Builder get", titleCase, "();\n"),

          kj::strTree(
              "inline ", fullName, "::Reader ", containingType, "::Reader::get", titleCase, "() const {\n"
              "  return ", fullName, "::Reader(_reader);\n"
              "}\n"
              "inline ", fullName, "::Builder ", containingType, "::Builder::get", titleCase, "() {\n"
              "  return ", fullName, "::Builder(_builder);\n"
              "}\n",
              kj::mv(subText.inlineMethodDefs)),

          kj::mv(subText.capnpPrivateDecls),
          kj::mv(subText.capnpPrivateDefs),
        };
      }
    }

    KJ_UNREACHABLE;
  }

  MembersText makeMembersText(kj::StringPtr namespace_, kj::StringPtr containingType,
                              StructSchema::MemberList members) {
    auto memberTexts = KJ_MAP(members, member) {
      return makeMemberText(namespace_, containingType, member);
    };

    return MembersText {
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.innerTypeDecls); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.innerTypeDefs); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.innerTypeReaderBuilderDefs); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.readerMethodDecls); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.builderMethodDecls); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.inlineMethodDefs); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.capnpPrivateDecls); }),
      kj::strTree(KJ_MAP(memberTexts, m) { return kj::mv(m.capnpPrivateDefs); }),
    };
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
  };

  NodeText makeNodeText(kj::StringPtr namespace_, kj::StringPtr scope,
                        kj::StringPtr name, Schema schema) {
    auto proto = schema.getProto();
    auto fullName = kj::str(scope, name);
    auto subScope = kj::str(fullName, "::");
    auto nestedTexts = KJ_MAP(proto.getNestedNodes(), nested) {
      return makeNodeText(namespace_, subScope, nested.getName(), schemaLoader.get(nested.getId()));
    };
    auto hexId = kj::hex(proto.getId());

    kj::ArrayPtr<const word> rawSchema = schema.asUncheckedMessage();

    // Convert the encoded schema to a literal byte array.
    auto schemaLiteral = kj::StringTree(KJ_MAP(rawSchema, w) {
      const byte* bytes = reinterpret_cast<const byte*>(&w);

      return kj::strTree(KJ_MAP(kj::range<uint>(0, sizeof(word)), i) {
        auto text = kj::toCharSequence(kj::implicitCast<uint>(bytes[i]));
        return kj::strTree(kj::repeat(' ', 4 - text.size()), text, ",");
      });
    }, "\n   ");

    auto schemaDecl = kj::strTree(
        "extern const ::capnp::_::RawSchema s_", hexId, ";\n");

    std::set<uint64_t> deps;
    enumerateDeps(proto, deps);

    kj::Vector<capnp::_::RawSchema::MemberInfo> memberInfos;
    switch (proto.getBody().which()) {
      case schema::Node::Body::STRUCT_NODE:
        makeMemberInfoTable(0, schema.asStruct().getMembers(), memberInfos);
        break;
      case schema::Node::Body::ENUM_NODE:
        makeMemberInfoTable(0, schema.asEnum().getEnumerants(), memberInfos);
        break;
      case schema::Node::Body::INTERFACE_NODE:
        makeMemberInfoTable(0, schema.asInterface().getMethods(), memberInfos);
        break;
      default:
        break;
    }

    auto schemaDef = kj::strTree(
        "static const ::capnp::_::AlignedData<", rawSchema.size(), "> b_", hexId, " = {\n"
        "  {", kj::mv(schemaLiteral), " }\n"
        "};\n"
        "static const ::capnp::_::RawSchema* const d_", hexId, "[] = {\n",
        KJ_MAP(deps, depId) {
          return kj::strTree("  &s_", kj::hex(depId), ",\n");
        },
        "};\n"
        "static const ::capnp::_::RawSchema::MemberInfo m_", hexId, "[] = {\n",
        KJ_MAP(memberInfos, info) {
          return kj::strTree("  { ", info.unionIndex, ", ", info.index, " },\n");
        },
        "};\n"
        "const ::capnp::_::RawSchema s_", hexId, " = {\n"
        "  0x", hexId, ", b_", hexId, ".words, ", rawSchema.size(), ", d_", hexId, ", m_", hexId, ",\n"
        "  ", deps.size(), ", ", memberInfos.size(), ", nullptr, nullptr\n"
        "};\n");

    switch (proto.getBody().which()) {
      case schema::Node::Body::FILE_NODE:
        KJ_FAIL_REQUIRE("This method shouldn't be called on file nodes.");

      case schema::Node::Body::STRUCT_NODE: {
        auto membersText = makeMembersText(namespace_, fullName, schema.asStruct().getMembers());

        auto structNode = proto.getBody().getStructNode();

        return NodeText {
          kj::strTree(
              "  struct ", name, ";\n"),

          kj::strTree(
              "struct ", scope, name, " {\n",
              "  ", name, "() = delete;\n"
              "\n"
              "  class Reader;\n"
              "  class Builder;\n",
              kj::mv(membersText.innerTypeDecls),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.outerTypeDecl); },
              "};\n"
              "\n",
              kj::mv(membersText.innerTypeDefs),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.outerTypeDef); }),

          kj::strTree(
              makeReaderDef(fullName, name, "structString",
                            kj::mv(membersText.readerMethodDecls)),
              makeBuilderDef(fullName, name, "structString",
                             kj::mv(membersText.builderMethodDecls)),
              kj::mv(membersText.innerTypeReaderBuilderDefs),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.readerBuilderDefs); }),

          kj::strTree(
              kj::mv(membersText.inlineMethodDefs),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.inlineMethodDefs); }),

          kj::strTree(
              kj::mv(schemaDecl),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.capnpSchemaDecls); }),

          kj::strTree(
              kj::mv(schemaDef),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.capnpSchemaDefs); }),

          kj::strTree(
              "CAPNP_DECLARE_STRUCT(\n"
              "    ", namespace_, "::", fullName, ", ", hexId, ",\n"
              "    ", structNode.getDataSectionWordSize(), ", ",
                      structNode.getPointerSectionSize(), ", ",
                      FIELD_SIZE_NAMES[static_cast<uint>(structNode.getPreferredListEncoding())],
                      ");\n",
              kj::mv(membersText.capnpPrivateDecls),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.capnpPrivateDecls); }),

          kj::strTree(
              "CAPNP_DEFINE_STRUCT(\n"
              "    ", namespace_, "::", fullName, ");\n",
              kj::mv(membersText.capnpPrivateDefs),
              KJ_MAP(nestedTexts, n) { return kj::mv(n.capnpPrivateDefs); }),
        };
      }

      case schema::Node::Body::ENUM_NODE: {
        auto enumerants = schema.asEnum().getEnumerants();

        return NodeText {
          scope.size() == 0 ? kj::strTree() : kj::strTree(
              "  enum class ", name, ": uint16_t {\n",
              KJ_MAP(enumerants, e) {
                return kj::strTree("    ", toUpperCase(e.getProto().getName()), ",\n");
              },
              "  };\n"
              "\n"),

          scope.size() > 0 ? kj::strTree() : kj::strTree(
              "enum class ", name, ": uint16_t {\n",
              KJ_MAP(enumerants, e) {
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
        };
      }

      case schema::Node::Body::INTERFACE_NODE: {
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
        };
      }

      case schema::Node::Body::CONST_NODE: {
        return NodeText {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),

          kj::mv(schemaDecl),
          kj::mv(schemaDef),

          kj::strTree(),
          kj::strTree(),
        };
      }

      case schema::Node::Body::ANNOTATION_NODE: {
        return NodeText {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),

          kj::mv(schemaDecl),
          kj::mv(schemaDef),

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

  FileText makeFileText(Schema schema) {
    usedImports.clear();

    auto node = schema.getProto();
    auto displayName = node.getDisplayName();

    kj::Vector<kj::ArrayPtr<const char>> namespaceParts;
    kj::String namespacePrefix;

    for (auto annotation: node.getAnnotations()) {
      if (annotation.getId() == NAMESPACE_ANNOTATION_ID) {
        kj::StringPtr ns = annotation.getValue().getBody().getTextValue();
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

    auto nodeTexts = KJ_MAP(node.getNestedNodes(), nested) {
      return makeNodeText(namespacePrefix, "", nested.getName(), schemaLoader.get(nested.getId()));
    };

    kj::String separator = kj::str("// ", kj::repeat('=', 87), "\n");

    kj::Vector<kj::StringPtr> includes;
    for (auto import: node.getBody().getFileNode().getImports()) {
      if (usedImports.count(import.getId()) > 0) {
        includes.add(import.getName());
      }
    }

    return FileText {
      kj::strTree(
          "// Generated by Cap'n Proto compiler, DO NOT EDIT\n"
          "// source: ", baseName(displayName), "\n"
          "\n"
          "#ifndef CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n",
          "#define CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n"
          "\n"
          "#include <capnp/generated-header-support.h>\n",
          KJ_MAP(includes, path) {
            if (path.startsWith("/")) {
              return kj::strTree("#include <", path.slice(1), ".h>\n");
            } else {
              return kj::strTree("#include \"", path, ".h\"\n");
            }
          },
          "\n",

          KJ_MAP(namespaceParts, n) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.outerTypeDef); },
          KJ_MAP(namespaceParts, n) { return kj::strTree("}  // namespace\n"); }, "\n",

          separator, "\n"
          "namespace capnp {\n"
          "namespace schemas {\n"
          "\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.capnpSchemaDecls); },
          "\n"
          "}  // namespace schemas\n"
          "namespace _ {  // private\n"
          "\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.capnpPrivateDecls); },
          "\n"
          "}  // namespace _ (private)\n"
          "}  // namespace capnp\n"

          "\n", separator, "\n",
          KJ_MAP(namespaceParts, n) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.readerBuilderDefs); },
          separator, "\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.inlineMethodDefs); },
          KJ_MAP(namespaceParts, n) { return kj::strTree("}  // namespace\n"); }, "\n",
          "#endif  // CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n"),

      kj::strTree(
          "// Generated by Cap'n Proto compiler, DO NOT EDIT\n"
          "// source: ", baseName(displayName), "\n"
          "\n"
          "#include \"", baseName(displayName), ".h\"\n"
          "\n"
          "namespace capnp {\n"
          "namespace schemas {\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.capnpSchemaDefs); },
          "}  // namespace schemas\n"
          "namespace _ {  // private\n",
          KJ_MAP(nodeTexts, n) { return kj::mv(n.capnpPrivateDefs); },
          "}  // namespace _ (private)\n"
          "}  // namespace capnp\n")
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

    for (auto fileId: request.getRequestedFiles()) {
      auto schema = schemaLoader.get(fileId);
      auto fileText = makeFileText(schema);

      writeFile(kj::str(schema.getProto().getDisplayName(), ".h"), fileText.header);
      writeFile(kj::str(schema.getProto().getDisplayName(), ".c++"), fileText.source);
    }

    return true;
  }
};

}  // namespace
}  // namespace capnp

KJ_MAIN(capnp::CapnpcCppMain);
