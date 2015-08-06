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

#include "schema.h"
#include "message.h"
#include <kj/debug.h>

namespace capnp {

namespace _ {  // private

// Null schemas generated using the below schema file with:
//
//     capnp eval -Isrc null-schemas.capnp node --flat |
//         hexdump -v -e '8/1 "0x%02x, "' -e '1/8 "\n"'; echo
//
// I totally don't understand hexdump format strings and came up with this command based on trial
// and error.
//
//     @0x879863d4b2cc4a1e;
//
//     using Node = import "/capnp/schema.capnp".Node;
//
//     const node :Node = (
//         id = 0x0000000000000000,
//         displayName = "(null schema)");
//
//     const struct :Node = (
//         id = 0x0000000000000001,
//         displayName = "(null struct schema)",
//         struct = (
//             dataWordCount = 0,
//             pointerCount = 0,
//             preferredListEncoding = empty));
//
//     const enum :Node = (
//         id = 0x0000000000000002,
//         displayName = "(null enum schema)",
//         enum = ());
//
//     const interface :Node = (
//         id = 0x0000000000000003,
//         displayName = "(null interface schema)",
//         interface = ());
//
//     const const :Node = (
//         id = 0x0000000000000004,
//         displayName = "(null const schema)",
//         const = (type = (void = void), value = (void = void)));

static const AlignedData<13> NULL_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,   // union discriminant intentionally mangled
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x73, 0x63,
  0x68, 0x65, 0x6d, 0x61, 0x29, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_SCHEMA = {
  0x0000000000000000, NULL_SCHEMA_BYTES.words, 13,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr,
  { &NULL_SCHEMA, nullptr, nullptr, 0, 0, nullptr }
};

static const AlignedData<14> NULL_STRUCT_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0xaa, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x73, 0x74,
  0x72, 0x75, 0x63, 0x74, 0x20, 0x73, 0x63, 0x68,
  0x65, 0x6d, 0x61, 0x29, 0x00, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_STRUCT_SCHEMA = {
  0x0000000000000001, NULL_STRUCT_SCHEMA_BYTES.words, 14,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr,
  { &NULL_STRUCT_SCHEMA, nullptr, nullptr, 0, 0, nullptr }
};

static const AlignedData<14> NULL_ENUM_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x9a, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x65, 0x6e,
  0x75, 0x6d, 0x20, 0x73, 0x63, 0x68, 0x65, 0x6d,
  0x61, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_ENUM_SCHEMA = {
  0x0000000000000002, NULL_ENUM_SCHEMA_BYTES.words, 14,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr,
  { &NULL_ENUM_SCHEMA, nullptr, nullptr, 0, 0, nullptr }
};

static const AlignedData<14> NULL_INTERFACE_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0xc2, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x69, 0x6e,
  0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x20,
  0x73, 0x63, 0x68, 0x65, 0x6d, 0x61, 0x29, 0x00,
}};
const RawSchema NULL_INTERFACE_SCHEMA = {
  0x0000000000000003, NULL_INTERFACE_SCHEMA_BYTES.words, 14,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr,
  { &NULL_INTERFACE_SCHEMA, nullptr, nullptr, 0, 0, nullptr }
};

static const AlignedData<20> NULL_CONST_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0xa2, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
  0x18, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x63, 0x6f,
  0x6e, 0x73, 0x74, 0x20, 0x73, 0x63, 0x68, 0x65,
  0x6d, 0x61, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_CONST_SCHEMA = {
  0x0000000000000004, NULL_CONST_SCHEMA_BYTES.words, 20,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr,
  { &NULL_CONST_SCHEMA, nullptr, nullptr, 0, 0, nullptr }
};

}  // namespace _ (private)

// =======================================================================================

schema::Node::Reader Schema::getProto() const {
  return readMessageUnchecked<schema::Node>(raw->generic->encodedNode);
}

kj::ArrayPtr<const word> Schema::asUncheckedMessage() const {
  return kj::arrayPtr(raw->generic->encodedNode, raw->generic->encodedSize);
}

Schema Schema::getDependency(uint64_t id, uint location) const {
  {
    // Binary search dependency list.
    uint lower = 0;
    uint upper = raw->dependencyCount;

    while (lower < upper) {
      uint mid = (lower + upper) / 2;

      auto candidate = raw->dependencies[mid];
      if (candidate.location == location) {
        candidate.schema->ensureInitialized();
        return Schema(candidate.schema);
      } else if (candidate.location < location) {
        lower = mid + 1;
      } else {
        upper = mid;
      }
    }
  }

  {
    uint lower = 0;
    uint upper = raw->generic->dependencyCount;

    while (lower < upper) {
      uint mid = (lower + upper) / 2;

      const _::RawSchema* candidate = raw->generic->dependencies[mid];

      uint64_t candidateId = candidate->id;
      if (candidateId == id) {
        candidate->ensureInitialized();
        return Schema(&candidate->defaultBrand);
      } else if (candidateId < id) {
        lower = mid + 1;
      } else {
        upper = mid;
      }
    }
  }

  KJ_FAIL_REQUIRE("Requested ID not found in dependency table.", kj::hex(id)) {
    return Schema();
  }
}

Schema::BrandArgumentList Schema::getBrandArgumentsAtScope(uint64_t scopeId) const {
  KJ_REQUIRE(getProto().getIsGeneric(), "Not a generic type.", getProto().getDisplayName());

  for (auto scope: kj::range(raw->scopes, raw->scopes + raw->scopeCount)) {
    if (scope->typeId == scopeId) {
      // OK, this scope matches the scope we're looking for.
      if (scope->isUnbound) {
        return BrandArgumentList(scopeId, true);
      } else {
        return BrandArgumentList(scopeId, scope->bindingCount, scope->bindings);
      }
    }
  }

  // This scope is not listed in the scopes list.
  return BrandArgumentList(scopeId, raw->isUnbound());
}

StructSchema Schema::asStruct() const {
  KJ_REQUIRE(getProto().isStruct(), "Tried to use non-struct schema as a struct.",
             getProto().getDisplayName()) {
    return StructSchema();
  }
  return StructSchema(*this);
}

EnumSchema Schema::asEnum() const {
  KJ_REQUIRE(getProto().isEnum(), "Tried to use non-enum schema as an enum.",
             getProto().getDisplayName()) {
    return EnumSchema();
  }
  return EnumSchema(*this);
}

InterfaceSchema Schema::asInterface() const {
  KJ_REQUIRE(getProto().isInterface(), "Tried to use non-interface schema as an interface.",
             getProto().getDisplayName()) {
    return InterfaceSchema();
  }
  return InterfaceSchema(*this);
}

ConstSchema Schema::asConst() const {
  KJ_REQUIRE(getProto().isConst(), "Tried to use non-constant schema as a constant.",
             getProto().getDisplayName()) {
    return ConstSchema();
  }
  return ConstSchema(*this);
}

kj::StringPtr Schema::getShortDisplayName() const {
  auto proto = getProto();
  return proto.getDisplayName().slice(proto.getDisplayNamePrefixLength());
}

void Schema::requireUsableAs(const _::RawSchema* expected) const {
  KJ_REQUIRE(raw->generic == expected ||
             (expected != nullptr && raw->generic->canCastTo == expected),
             "This schema is not compatible with the requested native type.");
}

uint32_t Schema::getSchemaOffset(const schema::Value::Reader& value) const {
  const word* ptr;

  switch (value.which()) {
    case schema::Value::TEXT:
      ptr = reinterpret_cast<const word*>(value.getText().begin());
      break;
    case schema::Value::DATA:
      ptr = reinterpret_cast<const word*>(value.getData().begin());
      break;
    case schema::Value::STRUCT:
      ptr = value.getStruct().getAs<_::UncheckedMessage>();
      break;
    case schema::Value::LIST:
      ptr = value.getList().getAs<_::UncheckedMessage>();
      break;
    case schema::Value::ANY_POINTER:
      ptr = value.getAnyPointer().getAs<_::UncheckedMessage>();
      break;
    default:
      KJ_FAIL_ASSERT("getDefaultValueSchemaOffset() can only be called on struct, list, "
                     "and any-pointer fields.");
  }

  return ptr - raw->generic->encodedNode;
}

Type Schema::getBrandBinding(uint64_t scopeId, uint index) const {
  return getBrandArgumentsAtScope(scopeId)[index];
}

Type Schema::interpretType(schema::Type::Reader proto, uint location) const {
  switch (proto.which()) {
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
    case schema::Type::TEXT:
    case schema::Type::DATA:
      return proto.which();

    case schema::Type::STRUCT: {
      auto structType = proto.getStruct();
      return getDependency(structType.getTypeId(), location).asStruct();
    }

    case schema::Type::ENUM: {
      auto enumType = proto.getEnum();
      return getDependency(enumType.getTypeId(), location).asEnum();
    }

    case schema::Type::INTERFACE: {
      auto interfaceType = proto.getInterface();
      return getDependency(interfaceType.getTypeId(), location).asInterface();
    }

    case schema::Type::LIST:
      return ListSchema::of(interpretType(proto.getList().getElementType(), location));

    case schema::Type::ANY_POINTER: {
      auto anyPointer = proto.getAnyPointer();
      switch (anyPointer.which()) {
        case schema::Type::AnyPointer::UNCONSTRAINED:
          return anyPointer.getUnconstrained().which();
        case schema::Type::AnyPointer::PARAMETER: {
          auto param = anyPointer.getParameter();
          return getBrandBinding(param.getScopeId(), param.getParameterIndex());
        }
        case schema::Type::AnyPointer::IMPLICIT_METHOD_PARAMETER:
          return Type(Type::ImplicitParameter {
              anyPointer.getImplicitMethodParameter().getParameterIndex() });
      }

      KJ_UNREACHABLE;
    }
  }

  KJ_UNREACHABLE;
}

Type Schema::BrandArgumentList::operator[](uint index) const {
  if (isUnbound) {
    return Type::BrandParameter { scopeId, index };
  }

  if (index >= size_) {
    // Binding index out-of-range. Treat as AnyPointer. This is important to allow new
    // type parameters to be added to existing types without breaking dependent
    // schemas.
    return schema::Type::ANY_POINTER;
  }

  auto& binding = bindings[index];
  Type result;
  if (binding.which == (uint)schema::Type::ANY_POINTER) {
    if (binding.scopeId != 0) {
      result = Type::BrandParameter { binding.scopeId, binding.paramIndex };
    } else if (binding.isImplicitParameter) {
      result = Type::ImplicitParameter { binding.paramIndex };
    } else {
      result = static_cast<schema::Type::AnyPointer::Unconstrained::Which>(binding.paramIndex);
    }
  } else if (binding.schema == nullptr) {
    // Builtin / primitive type.
    result = static_cast<schema::Type::Which>(binding.which);
  } else {
    binding.schema->ensureInitialized();
    result = Type(static_cast<schema::Type::Which>(binding.which), binding.schema);
  }

  return result.wrapInList(binding.listDepth);
}

kj::StringPtr KJ_STRINGIFY(const Schema& schema) {
  return schema.getProto().getDisplayName();
}

// =======================================================================================

namespace {

template <typename List>
auto findSchemaMemberByName(const _::RawSchema* raw, kj::StringPtr name, List&& list)
    -> kj::Maybe<decltype(list[0])> {
  uint lower = 0;
  uint upper = raw->memberCount;

  while (lower < upper) {
    uint mid = (lower + upper) / 2;

    uint16_t memberIndex = raw->membersByName[mid];

    auto candidate = list[memberIndex];
    kj::StringPtr candidateName = candidate.getProto().getName();
    if (candidateName == name) {
      return candidate;
    } else if (candidateName < name) {
      lower = mid + 1;
    } else {
      upper = mid;
    }
  }

  return nullptr;
}

}  // namespace

StructSchema::FieldList StructSchema::getFields() const {
  return FieldList(*this, getProto().getStruct().getFields());
}

StructSchema::FieldSubset StructSchema::getUnionFields() const {
  auto proto = getProto().getStruct();
  return FieldSubset(*this, proto.getFields(),
                     raw->generic->membersByDiscriminant, proto.getDiscriminantCount());
}

StructSchema::FieldSubset StructSchema::getNonUnionFields() const {
  auto proto = getProto().getStruct();
  auto fields = proto.getFields();
  auto offset = proto.getDiscriminantCount();
  auto size = fields.size() - offset;
  return FieldSubset(*this, fields, raw->generic->membersByDiscriminant + offset, size);
}

kj::Maybe<StructSchema::Field> StructSchema::findFieldByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw->generic, name, getFields());
}

StructSchema::Field StructSchema::getFieldByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(member, findFieldByName(name)) {
    return *member;
  } else {
    KJ_FAIL_REQUIRE("struct has no such member", name);
  }
}

kj::Maybe<StructSchema::Field> StructSchema::getFieldByDiscriminant(uint16_t discriminant) const {
  auto unionFields = getUnionFields();

  if (discriminant >= unionFields.size()) {
    return nullptr;
  } else {
    return unionFields[discriminant];
  }
}

Type StructSchema::Field::getType() const {
  auto proto = getProto();
  uint location = _::RawBrandedSchema::makeDepLocation(_::RawBrandedSchema::DepKind::FIELD, index);

  switch (proto.which()) {
    case schema::Field::SLOT:
      return parent.interpretType(proto.getSlot().getType(), location);

    case schema::Field::GROUP:
      return parent.getDependency(proto.getGroup().getTypeId(), location).asStruct();
  }
  KJ_UNREACHABLE;
}

uint32_t StructSchema::Field::getDefaultValueSchemaOffset() const {
  return parent.getSchemaOffset(proto.getSlot().getDefaultValue());
}

kj::StringPtr KJ_STRINGIFY(const StructSchema::Field& field) {
  return field.getProto().getName();
}

// -------------------------------------------------------------------

EnumSchema::EnumerantList EnumSchema::getEnumerants() const {
  return EnumerantList(*this, getProto().getEnum().getEnumerants());
}

kj::Maybe<EnumSchema::Enumerant> EnumSchema::findEnumerantByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw->generic, name, getEnumerants());
}

EnumSchema::Enumerant EnumSchema::getEnumerantByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(enumerant, findEnumerantByName(name)) {
    return *enumerant;
  } else {
    KJ_FAIL_REQUIRE("enum has no such enumerant", name);
  }
}

// -------------------------------------------------------------------

InterfaceSchema::MethodList InterfaceSchema::getMethods() const {
  return MethodList(*this, getProto().getInterface().getMethods());
}

kj::Maybe<InterfaceSchema::Method> InterfaceSchema::findMethodByName(kj::StringPtr name) const {
  uint counter = 0;
  return findMethodByName(name, counter);
}

static constexpr uint MAX_SUPERCLASSES = 64;

kj::Maybe<InterfaceSchema::Method> InterfaceSchema::findMethodByName(
    kj::StringPtr name, uint& counter) const {
  // Security:  Don't let someone DOS us with a dynamic schema containing cyclic inheritance.
  KJ_REQUIRE(counter++ < MAX_SUPERCLASSES, "Cyclic or absurdly-large inheritance graph detected.") {
    return nullptr;
  }

  auto result = findSchemaMemberByName(raw->generic, name, getMethods());

  if (result == nullptr) {
    // Search superclasses.
    // TODO(perf):  This may be somewhat slow, and in the case of lots of diamond dependencies it
    //   could get pathological.  Arguably we should generate a flat list of transitive
    //   superclasses to search and store it in the RawSchema.  It's problematic, though, because
    //   this means that a dynamically-loaded RawSchema cannot be correctly constructed until all
    //   superclasses have been loaded, which imposes an ordering requirement on SchemaLoader or
    //   requires updating subclasses whenever a new superclass is loaded.
    auto superclasses = getProto().getInterface().getSuperclasses();
    for (auto i: kj::indices(superclasses)) {
      auto superclass = superclasses[i];
      uint location = _::RawBrandedSchema::makeDepLocation(
          _::RawBrandedSchema::DepKind::SUPERCLASS, i);
      result = getDependency(superclass.getId(), location)
          .asInterface().findMethodByName(name, counter);
      if (result != nullptr) {
        break;
      }
    }
  }

  return result;
}

InterfaceSchema::Method InterfaceSchema::getMethodByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(method, findMethodByName(name)) {
    return *method;
  } else {
    KJ_FAIL_REQUIRE("interface has no such method", name);
  }
}

InterfaceSchema::SuperclassList InterfaceSchema::getSuperclasses() const {
  return SuperclassList(*this, getProto().getInterface().getSuperclasses());
}

bool InterfaceSchema::extends(InterfaceSchema other) const {
  if (other.raw->generic == &_::NULL_INTERFACE_SCHEMA) {
    // We consider all interfaces to extend the null schema.
    return true;
  }
  uint counter = 0;
  return extends(other, counter);
}

bool InterfaceSchema::extends(InterfaceSchema other, uint& counter) const {
  // Security:  Don't let someone DOS us with a dynamic schema containing cyclic inheritance.
  KJ_REQUIRE(counter++ < MAX_SUPERCLASSES, "Cyclic or absurdly-large inheritance graph detected.") {
    return false;
  }

  if (other == *this) {
    return true;
  }

  // TODO(perf):  This may be somewhat slow.  See findMethodByName() for discussion.
  auto superclasses = getProto().getInterface().getSuperclasses();
  for (auto i: kj::indices(superclasses)) {
    auto superclass = superclasses[i];
    uint location = _::RawBrandedSchema::makeDepLocation(
        _::RawBrandedSchema::DepKind::SUPERCLASS, i);
    if (getDependency(superclass.getId(), location).asInterface().extends(other, counter)) {
      return true;
    }
  }

  return false;
}

kj::Maybe<InterfaceSchema> InterfaceSchema::findSuperclass(uint64_t typeId) const {
  if (typeId == _::NULL_INTERFACE_SCHEMA.id) {
    // We consider all interfaces to extend the null schema.
    return InterfaceSchema();
  }
  uint counter = 0;
  return findSuperclass(typeId, counter);
}

kj::Maybe<InterfaceSchema> InterfaceSchema::findSuperclass(uint64_t typeId, uint& counter) const {
  // Security:  Don't let someone DOS us with a dynamic schema containing cyclic inheritance.
  KJ_REQUIRE(counter++ < MAX_SUPERCLASSES, "Cyclic or absurdly-large inheritance graph detected.") {
    return nullptr;
  }

  if (typeId == raw->generic->id) {
    return *this;
  }

  // TODO(perf):  This may be somewhat slow.  See findMethodByName() for discussion.
  auto superclasses = getProto().getInterface().getSuperclasses();
  for (auto i: kj::indices(superclasses)) {
    auto superclass = superclasses[i];
    uint location = _::RawBrandedSchema::makeDepLocation(
        _::RawBrandedSchema::DepKind::SUPERCLASS, i);
    KJ_IF_MAYBE(result, getDependency(superclass.getId(), location).asInterface()
                            .findSuperclass(typeId, counter)) {
      return *result;
    }
  }

  return nullptr;
}

StructSchema InterfaceSchema::Method::getParamType() const {
  auto proto = getProto();
  uint location = _::RawBrandedSchema::makeDepLocation(
      _::RawBrandedSchema::DepKind::METHOD_PARAMS, ordinal);
  return parent.getDependency(proto.getParamStructType(), location).asStruct();
}

StructSchema InterfaceSchema::Method::getResultType() const {
  auto proto = getProto();
  uint location = _::RawBrandedSchema::makeDepLocation(
      _::RawBrandedSchema::DepKind::METHOD_RESULTS, ordinal);
  return parent.getDependency(proto.getResultStructType(), location).asStruct();
}

InterfaceSchema InterfaceSchema::SuperclassList::operator[](uint index) const {
  auto superclass = list[index];
  uint location = _::RawBrandedSchema::makeDepLocation(
      _::RawBrandedSchema::DepKind::SUPERCLASS, index);
  return parent.getDependency(superclass.getId(), location).asInterface();
}

// -------------------------------------------------------------------

uint32_t ConstSchema::getValueSchemaOffset() const {
  return getSchemaOffset(getProto().getConst().getValue());
}

Type ConstSchema::getType() const {
  return interpretType(getProto().getConst().getType(),
      _::RawBrandedSchema::makeDepLocation(_::RawBrandedSchema::DepKind::CONST_TYPE, 0));
}

// =======================================================================================

ListSchema ListSchema::of(schema::Type::Which primitiveType) {
  switch (primitiveType) {
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
    case schema::Type::TEXT:
    case schema::Type::DATA:
      break;

    case schema::Type::STRUCT:
    case schema::Type::ENUM:
    case schema::Type::INTERFACE:
    case schema::Type::LIST:
      KJ_FAIL_REQUIRE("Must use one of the other ListSchema::of() overloads for complex types.");
      break;

    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("List(AnyPointer) not supported.");
      break;
  }

  return ListSchema(primitiveType);
}

ListSchema ListSchema::of(schema::Type::Reader elementType, Schema context) {
  // This method is deprecated because it can only be implemented in terms of other deprecated
  // methods. Temporarily disable warnings for those other deprecated methods.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

  switch (elementType.which()) {
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
    case schema::Type::TEXT:
    case schema::Type::DATA:
      return of(elementType.which());

    case schema::Type::STRUCT:
      return of(context.getDependency(elementType.getStruct().getTypeId()).asStruct());

    case schema::Type::ENUM:
      return of(context.getDependency(elementType.getEnum().getTypeId()).asEnum());

    case schema::Type::INTERFACE:
      return of(context.getDependency(elementType.getInterface().getTypeId()).asInterface());

    case schema::Type::LIST:
      return of(of(elementType.getList().getElementType(), context));

    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("List(AnyPointer) not supported.");
      return ListSchema();
  }

  // Unknown type is acceptable.
  return ListSchema(elementType.which());
#pragma GCC diagnostic pop
}

// =======================================================================================

StructSchema Type::asStruct() const {
  KJ_REQUIRE(isStruct(), "Tried to interpret a non-struct type as a struct.") {
    return StructSchema();
  }
  KJ_ASSERT(schema != nullptr);
  return StructSchema(Schema(schema));
}
EnumSchema Type::asEnum() const {
  KJ_REQUIRE(isEnum(), "Tried to interpret a non-enum type as an enum.") {
    return EnumSchema();
  }
  KJ_ASSERT(schema != nullptr);
  return EnumSchema(Schema(schema));
}
InterfaceSchema Type::asInterface() const {
  KJ_REQUIRE(isInterface(), "Tried to interpret a non-interface type as an interface.") {
    return InterfaceSchema();
  }
  KJ_ASSERT(schema != nullptr);
  return InterfaceSchema(Schema(schema));
}
ListSchema Type::asList() const {
  KJ_REQUIRE(isList(), "Type::asList(): Not a list.") {
    return ListSchema::of(schema::Type::VOID);
  }
  Type elementType = *this;
  --elementType.listDepth;
  return ListSchema::of(elementType);
}

kj::Maybe<Type::BrandParameter> Type::getBrandParameter() const {
  KJ_REQUIRE(isAnyPointer(), "Type::getBrandParameter() can only be called on AnyPointer types.");

  if (scopeId == 0) {
    return nullptr;
  } else {
    return BrandParameter { scopeId, paramIndex };
  }
}

kj::Maybe<Type::ImplicitParameter> Type::getImplicitParameter() const {
  KJ_REQUIRE(isAnyPointer(),
      "Type::getImplicitParameter() can only be called on AnyPointer types.");

  if (isImplicitParam) {
    return ImplicitParameter { paramIndex };
  } else {
    return nullptr;
  }
}

bool Type::operator==(const Type& other) const {
  if (baseType != other.baseType || listDepth != other.listDepth) {
    return false;
  }

  switch (baseType) {
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
    case schema::Type::TEXT:
    case schema::Type::DATA:
      return true;

    case schema::Type::STRUCT:
    case schema::Type::ENUM:
    case schema::Type::INTERFACE:
      return schema == other.schema;

    case schema::Type::LIST:
      KJ_UNREACHABLE;

    case schema::Type::ANY_POINTER:
      return scopeId == other.scopeId && isImplicitParam == other.isImplicitParam &&
          // Trying to comply with strict aliasing rules. Hopefully the compiler realizes that
          // both branches compile to the same instructions and can optimize it away.
          (scopeId != 0 || isImplicitParam ? paramIndex == other.paramIndex
                                           : anyPointerKind == other.anyPointerKind);
  }

  KJ_UNREACHABLE;
}

size_t Type::hashCode() const {
  switch (baseType) {
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
    case schema::Type::TEXT:
    case schema::Type::DATA:
      return (static_cast<size_t>(baseType) << 3) + listDepth;

    case schema::Type::STRUCT:
    case schema::Type::ENUM:
    case schema::Type::INTERFACE:
      return reinterpret_cast<size_t>(schema) + listDepth;

    case schema::Type::LIST:
      KJ_UNREACHABLE;

    case schema::Type::ANY_POINTER: {
      // Trying to comply with strict aliasing rules. Hopefully the compiler realizes that
      // both branches compile to the same instructions and can optimize it away.
      size_t val = scopeId != 0 || isImplicitParam ?
          paramIndex : static_cast<uint16_t>(anyPointerKind);
      return (val << 1 | isImplicitParam) ^ scopeId;
    }
  }

  KJ_UNREACHABLE;
}

void Type::requireUsableAs(Type expected) const {
  KJ_REQUIRE(baseType == expected.baseType && listDepth == expected.listDepth,
             "This type is not compatible with the requested native type.");

  switch (baseType) {
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
    case schema::Type::TEXT:
    case schema::Type::DATA:
    case schema::Type::ANY_POINTER:
      break;

    case schema::Type::STRUCT:
    case schema::Type::ENUM:
    case schema::Type::INTERFACE:
      Schema(schema).requireUsableAs(expected.schema->generic);
      break;

    case schema::Type::LIST:
      KJ_UNREACHABLE;
  }
}

}  // namespace capnp
