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

#define CAPNP_PRIVATE
#include "schema-loader.h"
#include <unordered_map>
#include <map>
#include "message.h"
#include "arena.h"
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/arena.h>

namespace capnp {

class SchemaLoader::InitializerImpl: public _::RawSchema::Initializer {
public:
  inline explicit InitializerImpl(const SchemaLoader& loader): loader(loader), callback(nullptr) {}
  inline InitializerImpl(const SchemaLoader& loader, const LazyLoadCallback& callback)
      : loader(loader), callback(callback) {}

  inline kj::Maybe<const LazyLoadCallback&> getCallback() const { return callback; }

  void init(const _::RawSchema* schema) const override;

  inline bool operator==(decltype(nullptr)) const { return callback == nullptr; }

private:
  const SchemaLoader& loader;
  kj::Maybe<const LazyLoadCallback&> callback;
};

class SchemaLoader::Impl {
public:
  inline explicit Impl(const SchemaLoader& loader): initializer(loader) {}
  inline Impl(const SchemaLoader& loader, const LazyLoadCallback& callback)
      : initializer(loader, callback) {}

  _::RawSchema* load(const schema::Node::Reader& reader, bool isPlaceholder);

  _::RawSchema* loadNative(const _::RawSchema* nativeSchema);

  _::RawSchema* loadEmpty(uint64_t id, kj::StringPtr name, schema::Node::Body::Which kind);
  // Create a dummy empty schema of the given kind for the given id and load it.

  struct TryGetResult {
    _::RawSchema* schema;
    kj::Maybe<const LazyLoadCallback&> callback;
  };

  TryGetResult tryGet(uint64_t typeId) const;
  kj::Array<Schema> getAllLoaded() const;

  kj::Arena arena;

private:
  std::unordered_map<uint64_t, _::RawSchema*> schemas;

  InitializerImpl initializer;
};

// =======================================================================================

class SchemaLoader::Validator {
public:
  Validator(SchemaLoader::Impl& loader): loader(loader) {}

  bool validate(const schema::Node::Reader& node) {
    isValid = true;
    nodeName = node.getDisplayName();
    dependencies.clear();

    KJ_CONTEXT("validating schema node", nodeName, (uint)node.getBody().which());

    switch (node.getBody().which()) {
      case schema::Node::Body::FILE_NODE:
        validate(node.getBody().getFileNode());
        break;
      case schema::Node::Body::STRUCT_NODE:
        validate(node.getBody().getStructNode());
        break;
      case schema::Node::Body::ENUM_NODE:
        validate(node.getBody().getEnumNode());
        break;
      case schema::Node::Body::INTERFACE_NODE:
        validate(node.getBody().getInterfaceNode());
        break;
      case schema::Node::Body::CONST_NODE:
        validate(node.getBody().getConstNode());
        break;
      case schema::Node::Body::ANNOTATION_NODE:
        validate(node.getBody().getAnnotationNode());
        break;
    }

    // We accept and pass through node types we don't recognize.
    return isValid;
  }

  const _::RawSchema** makeDependencyArray(uint32_t* count) {
    *count = dependencies.size();
    kj::ArrayPtr<const _::RawSchema*> result =
        loader.arena.allocateArray<const _::RawSchema*>(*count);
    uint pos = 0;
    for (auto& dep: dependencies) {
      result[pos++] = dep.second;
    }
    KJ_DASSERT(pos == *count);
    return result.begin();
  }

  const _::RawSchema::MemberInfo* makeMemberInfoArray(uint32_t* count) {
    *count = members.size();
    kj::ArrayPtr<_::RawSchema::MemberInfo> result =
        loader.arena.allocateArray<_::RawSchema::MemberInfo>(*count);
    uint pos = 0;
    for (auto& member: members) {
      result[pos++] = {kj::implicitCast<uint16_t>(member.first.first),
                       kj::implicitCast<uint16_t>(member.second)};
    }
    KJ_DASSERT(pos == *count);
    return result.begin();
  }

private:
  SchemaLoader::Impl& loader;
  Text::Reader nodeName;
  bool isValid;
  std::map<uint64_t, _::RawSchema*> dependencies;

  // Maps (unionIndex, name) -> index for each member.
  std::map<std::pair<uint, Text::Reader>, uint> members;

#define VALIDATE_SCHEMA(condition, ...) \
  KJ_REQUIRE(condition, ##__VA_ARGS__) { isValid = false; return; }
#define FAIL_VALIDATE_SCHEMA(...) \
  KJ_FAIL_REQUIRE(__VA_ARGS__) { isValid = false; return; }

  void validate(const schema::FileNode::Reader& fileNode) {
    // Nothing needs validation.
  }

  void validate(const schema::StructNode::Reader& structNode) {
    uint dataSizeInBits;
    uint pointerCount;

    switch (structNode.getPreferredListEncoding()) {
      case schema::ElementSize::EMPTY:
        dataSizeInBits = 0;
        pointerCount = 0;
        break;
      case schema::ElementSize::BIT:
        dataSizeInBits = 1;
        pointerCount = 0;
        break;
      case schema::ElementSize::BYTE:
        dataSizeInBits = 8;
        pointerCount = 0;
        break;
      case schema::ElementSize::TWO_BYTES:
        dataSizeInBits = 16;
        pointerCount = 0;
        break;
      case schema::ElementSize::FOUR_BYTES:
        dataSizeInBits = 32;
        pointerCount = 0;
        break;
      case schema::ElementSize::EIGHT_BYTES:
        dataSizeInBits = 64;
        pointerCount = 0;
        break;
      case schema::ElementSize::POINTER:
        dataSizeInBits = 0;
        pointerCount = 1;
        break;
      case schema::ElementSize::INLINE_COMPOSITE:
        dataSizeInBits = structNode.getDataSectionWordSize() * 64;
        pointerCount = structNode.getPointerSectionSize();
        break;
      default:
        FAIL_VALIDATE_SCHEMA("Invalid preferredListEncoding.");
        dataSizeInBits = 0;
        pointerCount = 0;
        break;
    }

    VALIDATE_SCHEMA(structNode.getDataSectionWordSize() == (dataSizeInBits + 63) / 64 &&
                    structNode.getPointerSectionSize() == pointerCount,
                    "Struct size does not match preferredListEncoding.");

    uint ordinalCount = 0;

    auto members = structNode.getMembers();
    for (auto member: members) {
      ++ordinalCount;
      if (member.getBody().which() == schema::StructNode::Member::Body::UNION_MEMBER) {
        ordinalCount += member.getBody().getUnionMember().getMembers().size();
      }
    }

    KJ_STACK_ARRAY(bool, sawCodeOrder, members.size(), 32, 256);
    memset(sawCodeOrder.begin(), 0, sawCodeOrder.size() * sizeof(sawCodeOrder[0]));
    KJ_STACK_ARRAY(bool, sawOrdinal, ordinalCount, 32, 256);
    memset(sawOrdinal.begin(), 0, sawOrdinal.size() * sizeof(sawOrdinal[0]));

    uint index = 0;
    for (auto member: members) {
      KJ_CONTEXT("validating struct member", member.getName());
      validate(member, sawCodeOrder, sawOrdinal, dataSizeInBits, pointerCount, 0, index++);
    }
  }

  void validateMemberName(kj::StringPtr name, uint unionIndex, uint index) {
    bool isNewName = members.insert(std::make_pair(
        std::pair<uint, Text::Reader>(unionIndex, name), index)).second;
    VALIDATE_SCHEMA(isNewName, "duplicate name", name);
  }

  void validate(const schema::StructNode::Member::Reader& member,
                kj::ArrayPtr<bool> sawCodeOrder, kj::ArrayPtr<bool> sawOrdinal,
                uint dataSizeInBits, uint pointerCount,
                uint unionIndex, uint index) {
    validateMemberName(member.getName(), unionIndex, index);

    VALIDATE_SCHEMA(member.getOrdinal() < sawOrdinal.size() &&
                    !sawOrdinal[member.getOrdinal()],
                    "Invalid ordinal.");
    sawOrdinal[member.getOrdinal()] = true;
    VALIDATE_SCHEMA(member.getCodeOrder() < sawCodeOrder.size() &&
                    !sawCodeOrder[member.getCodeOrder()],
                    "Invalid codeOrder.");
    sawCodeOrder[member.getCodeOrder()] = true;

    switch (member.getBody().which()) {
      case schema::StructNode::Member::Body::FIELD_MEMBER: {
        auto field = member.getBody().getFieldMember();

        uint fieldBits;
        bool fieldIsPointer;
        validate(field.getType(), field.getDefaultValue(), &fieldBits, &fieldIsPointer);
        VALIDATE_SCHEMA(fieldBits * (field.getOffset() + 1) <= dataSizeInBits &&
                        fieldIsPointer * (field.getOffset() + 1) <= pointerCount,
                        "field offset out-of-bounds",
                        field.getOffset(), dataSizeInBits, pointerCount);
        break;
      }

      case schema::StructNode::Member::Body::UNION_MEMBER: {
        auto u = member.getBody().getUnionMember();

        VALIDATE_SCHEMA((u.getDiscriminantOffset() + 1) * 16 <= dataSizeInBits,
                        "Schema invalid: Union discriminant out-of-bounds.");

        auto uMembers = u.getMembers();
        KJ_STACK_ARRAY(bool, uSawCodeOrder, uMembers.size(), 32, 256);
        memset(uSawCodeOrder.begin(), 0, uSawCodeOrder.size() * sizeof(uSawCodeOrder[0]));

        uint subIndex = 0;
        for (auto uMember: uMembers) {
          KJ_CONTEXT("validating union member", uMember.getName());
          VALIDATE_SCHEMA(
              uMember.getBody().which() == schema::StructNode::Member::Body::FIELD_MEMBER,
              "Union members must be fields.");
          validate(uMember, uSawCodeOrder, sawOrdinal, dataSizeInBits, pointerCount,
                   index + 1, subIndex++);
        }
        break;
      }
    }
  }

  void validate(const schema::EnumNode::Reader& enumNode) {
    auto enumerants = enumNode.getEnumerants();

    KJ_STACK_ARRAY(bool, sawCodeOrder, enumerants.size(), 32, 256);
    memset(sawCodeOrder.begin(), 0, sawCodeOrder.size() * sizeof(sawCodeOrder[0]));

    uint index = 0;
    for (auto enumerant: enumerants) {
      validateMemberName(enumerant.getName(), 0, index++);

      VALIDATE_SCHEMA(enumerant.getCodeOrder() < enumerants.size() &&
                      !sawCodeOrder[enumerant.getCodeOrder()],
                      "invalid codeOrder", enumerant.getName());
      sawCodeOrder[enumerant.getCodeOrder()] = true;
    }
  }

  void validate(const schema::InterfaceNode::Reader& interfaceNode) {
    auto methods = interfaceNode.getMethods();

    KJ_STACK_ARRAY(bool, sawCodeOrder, methods.size(), 32, 256);
    memset(sawCodeOrder.begin(), 0, sawCodeOrder.size() * sizeof(sawCodeOrder[0]));

    uint index = 0;
    for (auto method: methods) {
      KJ_CONTEXT("validating method", method.getName());
      validateMemberName(method.getName(), 0, index++);

      VALIDATE_SCHEMA(method.getCodeOrder() < methods.size() &&
                      !sawCodeOrder[method.getCodeOrder()],
                      "invalid codeOrder");
      sawCodeOrder[method.getCodeOrder()] = true;

      auto params = method.getParams();
      for (auto param: params) {
        KJ_CONTEXT("validating parameter", param.getName());
        uint dummy1;
        bool dummy2;
        validate(param.getType(), param.getDefaultValue(), &dummy1, &dummy2);
      }

      VALIDATE_SCHEMA(method.getRequiredParamCount() <= params.size(),
                      "invalid requiredParamCount");
      validate(method.getReturnType());
    }
  }

  void validate(const schema::ConstNode::Reader& constNode) {
    uint dummy1;
    bool dummy2;
    validate(constNode.getType(), constNode.getValue(), &dummy1, &dummy2);
  }

  void validate(const schema::AnnotationNode::Reader& annotationNode) {
    validate(annotationNode.getType());
  }

  void validate(const schema::Type::Reader& type, const schema::Value::Reader& value,
                uint* dataSizeInBits, bool* isPointer) {
    validate(type);

    schema::Value::Body::Which expectedValueType = schema::Value::Body::VOID_VALUE;
    bool hadCase = false;
    switch (type.getBody().which()) {
#define HANDLE_TYPE(name, bits, ptr) \
      case schema::Type::Body::name##_TYPE: \
        expectedValueType = schema::Value::Body::name##_VALUE; \
        *dataSizeInBits = bits; *isPointer = ptr; \
        hadCase = true; \
        break;
      HANDLE_TYPE(VOID, 0, false)
      HANDLE_TYPE(BOOL, 1, false)
      HANDLE_TYPE(INT8, 8, false)
      HANDLE_TYPE(INT16, 16, false)
      HANDLE_TYPE(INT32, 32, false)
      HANDLE_TYPE(INT64, 64, false)
      HANDLE_TYPE(UINT8, 8, false)
      HANDLE_TYPE(UINT16, 16, false)
      HANDLE_TYPE(UINT32, 32, false)
      HANDLE_TYPE(UINT64, 64, false)
      HANDLE_TYPE(FLOAT32, 32, false)
      HANDLE_TYPE(FLOAT64, 64, false)
      HANDLE_TYPE(TEXT, 0, true)
      HANDLE_TYPE(DATA, 0, true)
      HANDLE_TYPE(LIST, 0, true)
      HANDLE_TYPE(ENUM, 16, false)
      HANDLE_TYPE(STRUCT, 0, true)
      HANDLE_TYPE(INTERFACE, 0, true)
      HANDLE_TYPE(OBJECT, 0, true)
#undef HANDLE_TYPE
    }

    if (hadCase) {
      VALIDATE_SCHEMA(value.getBody().which() == expectedValueType, "Value did not match type.");
    }
  }

  void validate(const schema::Type::Reader& type) {
    switch (type.getBody().which()) {
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
      case schema::Type::Body::TEXT_TYPE:
      case schema::Type::Body::DATA_TYPE:
      case schema::Type::Body::OBJECT_TYPE:
        break;

      case schema::Type::Body::STRUCT_TYPE:
        validateTypeId(type.getBody().getStructType(), schema::Node::Body::STRUCT_NODE);
        break;
      case schema::Type::Body::ENUM_TYPE:
        validateTypeId(type.getBody().getEnumType(), schema::Node::Body::ENUM_NODE);
        break;
      case schema::Type::Body::INTERFACE_TYPE:
        validateTypeId(type.getBody().getInterfaceType(), schema::Node::Body::INTERFACE_NODE);
        break;

      case schema::Type::Body::LIST_TYPE:
        validate(type.getBody().getListType());
        break;
    }

    // We intentionally allow unknown types.
  }

  void validateTypeId(uint64_t id, schema::Node::Body::Which expectedKind) {
    _::RawSchema* existing = loader.tryGet(id).schema;
    if (existing != nullptr) {
      auto node = readMessageUnchecked<schema::Node>(existing->encodedNode);
      VALIDATE_SCHEMA(node.getBody().which() == expectedKind,
          "expected a different kind of node for this ID",
          id, (uint)expectedKind, (uint)node.getBody().which(), node.getDisplayName());
      dependencies.insert(std::make_pair(id, existing));
      return;
    }

    dependencies.insert(std::make_pair(id, loader.loadEmpty(
        id, kj::str("(unknown type used by ", nodeName , ")"), expectedKind)));
  }

#undef VALIDATE_SCHEMA
#undef FAIL_VALIDATE_SCHEMA
};

// =======================================================================================

class SchemaLoader::CompatibilityChecker {
public:
  CompatibilityChecker(SchemaLoader::Impl& loader): loader(loader) {}

  bool shouldReplace(const schema::Node::Reader& existingNode,
                     const schema::Node::Reader& replacement,
                     bool preferReplacementIfEquivalent) {
    KJ_CONTEXT("checking compatibility with previously-loaded node of the same id",
               existingNode.getDisplayName());

    KJ_DREQUIRE(existingNode.getId() == replacement.getId());

    nodeName = existingNode.getDisplayName();
    compatibility = EQUIVALENT;

    checkCompatibility(existingNode, replacement);

    // Prefer the newer schema.
    return preferReplacementIfEquivalent ? compatibility != OLDER : compatibility == NEWER;
  }

private:
  SchemaLoader::Impl& loader;
  Text::Reader nodeName;

  enum Compatibility {
    EQUIVALENT,
    OLDER,
    NEWER,
    INCOMPATIBLE
  };
  Compatibility compatibility;

#define VALIDATE_SCHEMA(condition, ...) \
  KJ_REQUIRE(condition, ##__VA_ARGS__) { compatibility = INCOMPATIBLE; return; }
#define FAIL_VALIDATE_SCHEMA(...) \
  KJ_FAIL_REQUIRE(__VA_ARGS__) { compatibility = INCOMPATIBLE; return; }

  void replacementIsNewer() {
    switch (compatibility) {
      case EQUIVALENT:
        compatibility = NEWER;
        break;
      case OLDER:
        FAIL_VALIDATE_SCHEMA("Schema node contains some changes that are upgrades and some "
            "that are downgrades.  All changes must be in the same direction for compatibility.");
        break;
      case NEWER:
        break;
      case INCOMPATIBLE:
        break;
    }
  }

  void replacementIsOlder() {
    switch (compatibility) {
      case EQUIVALENT:
        compatibility = OLDER;
        break;
      case OLDER:
        break;
      case NEWER:
        FAIL_VALIDATE_SCHEMA("Schema node contains some changes that are upgrades and some "
            "that are downgrades.  All changes must be in the same direction for compatibility.");
        break;
      case INCOMPATIBLE:
        break;
    }
  }

  void checkCompatibility(const schema::Node::Reader& node,
                          const schema::Node::Reader& replacement) {
    // Returns whether `replacement` is equivalent, older than, newer than, or incompatible with
    // `node`.  If exceptions are enabled, this will throw an exception on INCOMPATIBLE.

    VALIDATE_SCHEMA(node.getBody().which() == replacement.getBody().which(),
                    "kind of declaration changed");

    // No need to check compatibility of the non-body parts of the node:
    // - Arbitrary renaming and moving between scopes is allowed.
    // - Annotations are ignored for compatibility purposes.

    switch (node.getBody().which()) {
      case schema::Node::Body::FILE_NODE:
        checkCompatibility(node.getBody().getFileNode(),
                           replacement.getBody().getFileNode());
        break;
      case schema::Node::Body::STRUCT_NODE:
        checkCompatibility(node.getBody().getStructNode(),
                           replacement.getBody().getStructNode());
        break;
      case schema::Node::Body::ENUM_NODE:
        checkCompatibility(node.getBody().getEnumNode(),
                           replacement.getBody().getEnumNode());
        break;
      case schema::Node::Body::INTERFACE_NODE:
        checkCompatibility(node.getBody().getInterfaceNode(),
                           replacement.getBody().getInterfaceNode());
        break;
      case schema::Node::Body::CONST_NODE:
        checkCompatibility(node.getBody().getConstNode(),
                           replacement.getBody().getConstNode());
        break;
      case schema::Node::Body::ANNOTATION_NODE:
        checkCompatibility(node.getBody().getAnnotationNode(),
                           replacement.getBody().getAnnotationNode());
        break;
    }
  }

  void checkCompatibility(const schema::FileNode::Reader& file,
                          const schema::FileNode::Reader& replacement) {
    // Nothing to compare.
  }

  void checkCompatibility(const schema::StructNode::Reader& structNode,
                          const schema::StructNode::Reader& replacement) {
    if (replacement.getDataSectionWordSize() > structNode.getDataSectionWordSize()) {
      replacementIsNewer();
    } else if (replacement.getDataSectionWordSize() < structNode.getDataSectionWordSize()) {
      replacementIsOlder();
    }
    if (replacement.getPointerSectionSize() > structNode.getPointerSectionSize()) {
      replacementIsNewer();
    } else if (replacement.getPointerSectionSize() < structNode.getPointerSectionSize()) {
      replacementIsOlder();
    }

    // We can do a simple comparison of preferredListEncoding here because the only case where it
    // isn't correct to compare this way is when one side is BIT/BYTE/*_BYTES while the other side
    // is POINTER, and if that were the case then the above comparisons would already have failed
    // or one of the nodes would have failed validation.
    if (replacement.getPreferredListEncoding() > structNode.getPreferredListEncoding()) {
      replacementIsNewer();
    } else if (replacement.getPreferredListEncoding() < structNode.getPreferredListEncoding()) {
      replacementIsOlder();
    }

    // The shared members should occupy corresponding positions in the member lists, since the
    // lists are sorted by ordinal.
    auto members = structNode.getMembers();
    auto replacementMembers = replacement.getMembers();
    uint count = std::min(members.size(), replacementMembers.size());

    if (replacementMembers.size() > members.size()) {
      replacementIsNewer();
    } else if (replacementMembers.size() < members.size()) {
      replacementIsOlder();
    }

    for (uint i = 0; i < count; i++) {
      checkCompatibility(members[i], replacementMembers[i]);
    }
  }

  void checkCompatibility(const schema::StructNode::Member::Reader& member,
                          const schema::StructNode::Member::Reader& replacement) {
    KJ_CONTEXT("comparing struct member", member.getName());

    switch (member.getBody().which()) {
      case schema::StructNode::Member::Body::FIELD_MEMBER: {
        auto field = member.getBody().getFieldMember();
        auto replacementField = replacement.getBody().getFieldMember();

        checkCompatibility(field.getType(), replacementField.getType(),
                           NO_UPGRADE_TO_STRUCT);
        checkDefaultCompatibility(field.getDefaultValue(), replacementField.getDefaultValue());

        VALIDATE_SCHEMA(field.getOffset() == replacementField.getOffset(),
                        "field position changed");
        break;
      }
      case schema::StructNode::Member::Body::UNION_MEMBER: {
        auto existingUnion = member.getBody().getUnionMember();
        auto replacementUnion = replacement.getBody().getUnionMember();

        VALIDATE_SCHEMA(
            existingUnion.getDiscriminantOffset() == replacementUnion.getDiscriminantOffset(),
            "union discriminant position changed");

        auto members = existingUnion.getMembers();
        auto replacementMembers = replacementUnion.getMembers();
        uint count = std::min(members.size(), replacementMembers.size());

        if (replacementMembers.size() > members.size()) {
          replacementIsNewer();
        } else if (replacementMembers.size() < members.size()) {
          replacementIsOlder();
        }

        for (uint i = 0; i < count; i++) {
          checkCompatibility(members[i], replacementMembers[i]);
        }
        break;
      }
    }
  }

  void checkCompatibility(const schema::EnumNode::Reader& enumNode,
                          const schema::EnumNode::Reader& replacement) {
    uint size = enumNode.getEnumerants().size();
    uint replacementSize = replacement.getEnumerants().size();
    if (replacementSize > size) {
      replacementIsNewer();
    } else if (replacementSize < size) {
      replacementIsOlder();
    }
  }

  void checkCompatibility(const schema::InterfaceNode::Reader& interfaceNode,
                          const schema::InterfaceNode::Reader& replacement) {
    auto methods = interfaceNode.getMethods();
    auto replacementMethods = replacement.getMethods();

    if (replacementMethods.size() > methods.size()) {
      replacementIsNewer();
    } else if (replacementMethods.size() < methods.size()) {
      replacementIsOlder();
    }

    uint count = std::min(methods.size(), replacementMethods.size());

    for (uint i = 0; i < count; i++) {
      checkCompatibility(methods[i], replacementMethods[i]);
    }
  }

  void checkCompatibility(const schema::InterfaceNode::Method::Reader& method,
                          const schema::InterfaceNode::Method::Reader& replacement) {
    KJ_CONTEXT("comparing method", method.getName());

    auto params = method.getParams();
    auto replacementParams = replacement.getParams();

    if (replacementParams.size() > params.size()) {
      replacementIsNewer();
    } else if (replacementParams.size() < params.size()) {
      replacementIsOlder();
    }

    uint count = std::min(params.size(), replacementParams.size());
    for (uint i = 0; i < count; i++) {
      auto param = params[i];
      auto replacementParam = replacementParams[i];

      KJ_CONTEXT("comparing parameter", param.getName());

      checkCompatibility(param.getType(), replacementParam.getType(),
                         NO_UPGRADE_TO_STRUCT);
      checkDefaultCompatibility(param.getDefaultValue(), replacementParam.getDefaultValue());
    }

    // Before checking that the required parameter counts are equal, check if the user added new
    // parameters without defaulting them, as this is the most common reason for this error and we
    // can provide a nicer error message.
    VALIDATE_SCHEMA(replacement.getRequiredParamCount() <= count &&
                    method.getRequiredParamCount() <= count,
        "Updated method signature contains additional parameters that lack default values");

    VALIDATE_SCHEMA(replacement.getRequiredParamCount() == method.getRequiredParamCount(),
        "Updated method signature has different number of required parameters (parameters without "
        "default values)");

    checkCompatibility(method.getReturnType(), replacement.getReturnType(),
                       ALLOW_UPGRADE_TO_STRUCT);
  }

  void checkCompatibility(const schema::ConstNode::Reader& constNode,
                          const schema::ConstNode::Reader& replacement) {
    // Who cares?  These don't appear on the wire.
  }

  void checkCompatibility(const schema::AnnotationNode::Reader& annotationNode,
                          const schema::AnnotationNode::Reader& replacement) {
    // Who cares?  These don't appear on the wire.
  }

  enum UpgradeToStructMode {
    ALLOW_UPGRADE_TO_STRUCT,
    NO_UPGRADE_TO_STRUCT
  };

  void checkCompatibility(const schema::Type::Reader& type,
                          const schema::Type::Reader& replacement,
                          UpgradeToStructMode upgradeToStructMode) {
    if (replacement.getBody().which() != type.getBody().which()) {
      // Check for allowed "upgrade" to Data or Object.
      if (replacement.getBody().which() == schema::Type::Body::DATA_TYPE &&
          canUpgradeToData(type)) {
        replacementIsNewer();
        return;
      } else if (type.getBody().which() == schema::Type::Body::DATA_TYPE &&
                 canUpgradeToData(replacement)) {
        replacementIsOlder();
        return;
      } else if (replacement.getBody().which() == schema::Type::Body::OBJECT_TYPE &&
                 canUpgradeToObject(type)) {
        replacementIsNewer();
        return;
      } else if (type.getBody().which() == schema::Type::Body::OBJECT_TYPE &&
                 canUpgradeToObject(replacement)) {
        replacementIsOlder();
        return;
      }

      if (upgradeToStructMode == ALLOW_UPGRADE_TO_STRUCT) {
        if (type.getBody().which() == schema::Type::Body::STRUCT_TYPE) {
          checkUpgradeToStruct(replacement, type.getBody().getStructType());
          return;
        } else if (replacement.getBody().which() == schema::Type::Body::STRUCT_TYPE) {
          checkUpgradeToStruct(type, replacement.getBody().getStructType());
          return;
        }
      }

      FAIL_VALIDATE_SCHEMA("a type was changed");
    }

    switch (type.getBody().which()) {
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
      case schema::Type::Body::TEXT_TYPE:
      case schema::Type::Body::DATA_TYPE:
      case schema::Type::Body::OBJECT_TYPE:
        return;

      case schema::Type::Body::LIST_TYPE:
        checkCompatibility(type.getBody().getListType(),
                           replacement.getBody().getListType(),
                           ALLOW_UPGRADE_TO_STRUCT);
        return;

      case schema::Type::Body::ENUM_TYPE:
        VALIDATE_SCHEMA(replacement.getBody().getEnumType() == type.getBody().getEnumType(),
                        "type changed enum type");
        return;

      case schema::Type::Body::STRUCT_TYPE:
        // TODO(someday):  If the IDs don't match, we should compare the two structs for
        //   compatibility.  This is tricky, though, because the new type's target may not yet be
        //   loaded.  In that case we could take the old type, make a copy of it, assign the new
        //   ID to the copy, and load() that.  That forces any struct type loaded for that ID to
        //   be compatible.  However, that has another problem, which is that it could be that the
        //   whole reason the type was replaced was to fork that type, and so an incompatibility
        //   could be very much expected.  This could be a rat hole...
        VALIDATE_SCHEMA(replacement.getBody().getStructType() == type.getBody().getStructType(),
                        "type changed to incompatible struct type");
        return;

      case schema::Type::Body::INTERFACE_TYPE:
        VALIDATE_SCHEMA(
            replacement.getBody().getInterfaceType() == type.getBody().getInterfaceType(),
            "type changed to incompatible interface type");
        return;
    }

    // We assume unknown types (from newer versions of Cap'n Proto?) are equivalent.
  }

  void checkUpgradeToStruct(const schema::Type::Reader& type, uint64_t structTypeId) {
    // We can't just look up the target struct and check it because it may not have been loaded
    // yet.  Instead, we contrive a struct that looks like what we want and load() that, which
    // guarantees that any incompatibility will be caught either now or when the real version of
    // that struct is loaded.

    word scratch[32];
    memset(scratch, 0, sizeof(scratch));
    MallocMessageBuilder builder(kj::arrayPtr(scratch, sizeof(scratch)));
    auto node = builder.initRoot<schema::Node>();
    node.setId(structTypeId);
    node.setDisplayName(kj::str("(unknown type used in ", nodeName, ")"));
    auto structNode = node.getBody().initStructNode();

    switch (type.getBody().which()) {
      case schema::Type::Body::VOID_TYPE:
        structNode.setDataSectionWordSize(0);
        structNode.setPointerSectionSize(0);
        structNode.setPreferredListEncoding(schema::ElementSize::EMPTY);
        break;

      case schema::Type::Body::BOOL_TYPE:
        structNode.setDataSectionWordSize(1);
        structNode.setPointerSectionSize(0);
        structNode.setPreferredListEncoding(schema::ElementSize::BIT);
        break;

      case schema::Type::Body::INT8_TYPE:
      case schema::Type::Body::UINT8_TYPE:
        structNode.setDataSectionWordSize(1);
        structNode.setPointerSectionSize(0);
        structNode.setPreferredListEncoding(schema::ElementSize::BYTE);
        break;

      case schema::Type::Body::INT16_TYPE:
      case schema::Type::Body::UINT16_TYPE:
      case schema::Type::Body::ENUM_TYPE:
        structNode.setDataSectionWordSize(1);
        structNode.setPointerSectionSize(0);
        structNode.setPreferredListEncoding(schema::ElementSize::TWO_BYTES);
        break;

      case schema::Type::Body::INT32_TYPE:
      case schema::Type::Body::UINT32_TYPE:
      case schema::Type::Body::FLOAT32_TYPE:
        structNode.setDataSectionWordSize(1);
        structNode.setPointerSectionSize(0);
        structNode.setPreferredListEncoding(schema::ElementSize::FOUR_BYTES);
        break;

      case schema::Type::Body::INT64_TYPE:
      case schema::Type::Body::UINT64_TYPE:
      case schema::Type::Body::FLOAT64_TYPE:
        structNode.setDataSectionWordSize(1);
        structNode.setPointerSectionSize(0);
        structNode.setPreferredListEncoding(schema::ElementSize::EIGHT_BYTES);
        break;

      case schema::Type::Body::TEXT_TYPE:
      case schema::Type::Body::DATA_TYPE:
      case schema::Type::Body::LIST_TYPE:
      case schema::Type::Body::STRUCT_TYPE:
      case schema::Type::Body::INTERFACE_TYPE:
      case schema::Type::Body::OBJECT_TYPE:
        structNode.setDataSectionWordSize(0);
        structNode.setPointerSectionSize(1);
        structNode.setPreferredListEncoding(schema::ElementSize::POINTER);
        break;
    }

    auto member = structNode.initMembers(1)[0];
    member.setName("member0");
    member.setOrdinal(0);
    member.setCodeOrder(0);
    member.getBody().initFieldMember().setType(type);

    loader.load(node, true);
  }

  bool canUpgradeToData(const schema::Type::Reader& type) {
    if (type.getBody().which() == schema::Type::Body::TEXT_TYPE) {
      return true;
    } else if (type.getBody().which() == schema::Type::Body::LIST_TYPE) {
      switch (type.getBody().getListType().getBody().which()) {
        case schema::Type::Body::INT8_TYPE:
        case schema::Type::Body::UINT8_TYPE:
          return true;
        default:
          return false;
      }
    } else {
      return false;
    }
  }

  bool canUpgradeToObject(const schema::Type::Reader& type) {
    switch (type.getBody().which()) {
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
        return false;

      case schema::Type::Body::TEXT_TYPE:
      case schema::Type::Body::DATA_TYPE:
      case schema::Type::Body::LIST_TYPE:
      case schema::Type::Body::STRUCT_TYPE:
      case schema::Type::Body::INTERFACE_TYPE:
      case schema::Type::Body::OBJECT_TYPE:
        return true;
    }

    // Be lenient with unknown types.
    return true;
  }

  void checkDefaultCompatibility(const schema::Value::Reader& value,
                                 const schema::Value::Reader& replacement) {
    // Note that we test default compatibility only after testing type compatibility, and default
    // values have already been validated as matching their types, so this should pass.
    KJ_ASSERT(value.getBody().which() == replacement.getBody().which()) {
      compatibility = INCOMPATIBLE;
      return;
    }

    switch (value.getBody().which()) {
#define HANDLE_TYPE(discrim, name) \
      case schema::Value::Body::discrim##_VALUE: \
        VALIDATE_SCHEMA(value.getBody().get##name##Value() == \
                        replacement.getBody().get##name##Value(), \
                        "default value changed"); \
        break;
      HANDLE_TYPE(VOID, Void);
      HANDLE_TYPE(BOOL, Bool);
      HANDLE_TYPE(INT8, Int8);
      HANDLE_TYPE(INT16, Int16);
      HANDLE_TYPE(INT32, Int32);
      HANDLE_TYPE(INT64, Int64);
      HANDLE_TYPE(UINT8, Uint8);
      HANDLE_TYPE(UINT16, Uint16);
      HANDLE_TYPE(UINT32, Uint32);
      HANDLE_TYPE(UINT64, Uint64);
      HANDLE_TYPE(FLOAT32, Float32);
      HANDLE_TYPE(FLOAT64, Float64);
      HANDLE_TYPE(ENUM, Enum);
#undef HANDLE_TYPE

      case schema::Value::Body::TEXT_VALUE:
      case schema::Value::Body::DATA_VALUE:
      case schema::Value::Body::LIST_VALUE:
      case schema::Value::Body::STRUCT_VALUE:
      case schema::Value::Body::INTERFACE_VALUE:
      case schema::Value::Body::OBJECT_VALUE:
        // It's not a big deal if default values for pointers change, and it would be difficult for
        // us to compare these defaults here, so just let it slide.
        break;
    }
  }
};

// =======================================================================================

_::RawSchema* SchemaLoader::Impl::load(const schema::Node::Reader& reader, bool isPlaceholder) {
  // Make a copy of the node which can be used unchecked.
  size_t size = reader.totalSizeInWords() + 1;
  kj::ArrayPtr<word> validated = arena.allocateArray<word>(size);
  memset(validated.begin(), 0, size * sizeof(word));
  copyToUnchecked(reader, validated);

  // Validate the copy.
  Validator validator(*this);
  auto validatedReader = readMessageUnchecked<schema::Node>(validated.begin());

  if (!validator.validate(validatedReader)) {
    // Not valid.  Construct an empty schema of the same type and return that.
    return loadEmpty(validatedReader.getId(),
                     validatedReader.getDisplayName(),
                     validatedReader.getBody().which());
  }

  // Check if we already have a schema for this ID.
  _::RawSchema*& slot = schemas[validatedReader.getId()];
  bool shouldReplace;
  if (slot == nullptr) {
    // Nope, allocate a new RawSchema.
    slot = &arena.allocate<_::RawSchema>();
    slot->id = validatedReader.getId();
    slot->canCastTo = nullptr;
    shouldReplace = true;
  } else {
    // Yes, check if it is compatible and figure out which schema is newer.

    if (slot->lazyInitializer == nullptr) {
      // The existing slot is not a placeholder, so whether we overwrite it or not, we cannot
      // end up with a placeholder.
      isPlaceholder = false;
    }

    auto existing = readMessageUnchecked<schema::Node>(slot->encodedNode);
    CompatibilityChecker checker(*this);

    // Prefer to replace the existing schema if the existing schema is a placeholder.  Otherwise,
    // prefer to keep the existing schema.
    shouldReplace = checker.shouldReplace(
        existing, validatedReader, slot->lazyInitializer != nullptr);
  }

  if (shouldReplace) {
    // Initialize the RawSchema.
    slot->encodedNode = validated.begin();
    slot->dependencies = validator.makeDependencyArray(&slot->dependencyCount);
    slot->membersByName = validator.makeMemberInfoArray(&slot->memberCount);
  }

  if (isPlaceholder) {
    slot->lazyInitializer = &initializer;
  } else {
    // If this schema is not newly-allocated, it may already be in the wild, specifically in the
    // dependency list of other schemas.  Once the initializer is null, it is live, so we must do
    // a release-store here.
    __atomic_store_n(&slot->lazyInitializer, nullptr, __ATOMIC_RELEASE);
  }

  return slot;
}

_::RawSchema* SchemaLoader::Impl::loadNative(const _::RawSchema* nativeSchema) {
  _::RawSchema*& slot = schemas[nativeSchema->id];
  bool shouldReplace;
  if (slot == nullptr) {
    slot = &arena.allocate<_::RawSchema>();
    shouldReplace = true;
  } else if (slot->canCastTo != nullptr) {
    // Already loaded natively, or we're currently in the process of loading natively and there
    // was a dependency cycle.
    KJ_REQUIRE(slot->canCastTo == nativeSchema,
        "two different compiled-in type have the same type ID",
        nativeSchema->id,
        readMessageUnchecked<schema::Node>(nativeSchema->encodedNode).getDisplayName(),
        readMessageUnchecked<schema::Node>(slot->canCastTo->encodedNode).getDisplayName());
    return slot;
  } else {
    auto existing = readMessageUnchecked<schema::Node>(slot->encodedNode);
    auto native = readMessageUnchecked<schema::Node>(nativeSchema->encodedNode);
    CompatibilityChecker checker(*this);
    shouldReplace = checker.shouldReplace(existing, native, true);
  }

  // Since we recurse below, the slot in the hash map could move around.  Copy out the pointer
  // for subsequent use.
  _::RawSchema* result = slot;

  if (shouldReplace) {
    // Set the schema to a copy of the native schema.
    *kj::implicitCast<_::RawSchema*>(result) = *nativeSchema;

    // Indicate that casting is safe.  Note that it's important to set this before recursively
    // loading dependencies, so that cycles don't cause infinite loops!
    result->canCastTo = nativeSchema;

    // Except that we need to set the dependency list to point at other loader-owned RawSchemas.
    kj::ArrayPtr<const _::RawSchema*> dependencies =
        arena.allocateArray<const _::RawSchema*>(result->dependencyCount);
    for (uint i = 0; i < nativeSchema->dependencyCount; i++) {
      dependencies[i] = loadNative(nativeSchema->dependencies[i]);
    }
    result->dependencies = dependencies.begin();
  } else {
    // The existing schema is newer.

    // Indicate that casting is safe.  Note that it's important to set this before recursively
    // loading dependencies, so that cycles don't cause infinite loops!
    result->canCastTo = nativeSchema;

    // Make sure the dependencies are loaded and compatible.
    for (uint i = 0; i < nativeSchema->dependencyCount; i++) {
      loadNative(nativeSchema->dependencies[i]);
    }
  }

  // If this schema is not newly-allocated, it may already be in the wild, specifically in the
  // dependency list of other schemas.  Once the initializer is null, it is live, so we must do
  // a release-store here.
  __atomic_store_n(&result->lazyInitializer, nullptr, __ATOMIC_RELEASE);

  return result;
}

_::RawSchema* SchemaLoader::Impl::loadEmpty(
    uint64_t id, kj::StringPtr name, schema::Node::Body::Which kind) {
  word scratch[32];
  memset(scratch, 0, sizeof(scratch));
  MallocMessageBuilder builder(kj::arrayPtr(scratch, sizeof(scratch)));
  auto node = builder.initRoot<schema::Node>();
  node.setId(id);
  node.setDisplayName(name);
  switch (kind) {
    case schema::Node::Body::STRUCT_NODE: node.getBody().initStructNode(); break;
    case schema::Node::Body::ENUM_NODE: node.getBody().initEnumNode(); break;
    case schema::Node::Body::INTERFACE_NODE: node.getBody().initInterfaceNode(); break;

    case schema::Node::Body::FILE_NODE:
    case schema::Node::Body::CONST_NODE:
    case schema::Node::Body::ANNOTATION_NODE:
      KJ_FAIL_REQUIRE("Not a type.");
      break;
  }

  return load(node, true);
}

SchemaLoader::Impl::TryGetResult SchemaLoader::Impl::tryGet(uint64_t typeId) const {
  auto iter = schemas.find(typeId);
  if (iter == schemas.end()) {
    return {nullptr, initializer.getCallback()};
  } else {
    return {iter->second, initializer.getCallback()};
  }
}

kj::Array<Schema> SchemaLoader::Impl::getAllLoaded() const {
  size_t count = 0;
  for (auto& schema: schemas) {
    if (schema.second->lazyInitializer == nullptr) ++count;
  }

  kj::Array<Schema> result = kj::heapArray<Schema>(count);
  size_t i = 0;
  for (auto& schema: schemas) {
    if (schema.second->lazyInitializer == nullptr) result[i++] = Schema(schema.second);
  }
  return result;
}

void SchemaLoader::InitializerImpl::init(const _::RawSchema* schema) const {
  KJ_IF_MAYBE(c, callback) {
    c->load(loader, schema->id);
  }

  if (schema->lazyInitializer != nullptr) {
    // The callback declined to load a schema.  We need to disable the initializer so that it
    // doesn't get invoked again later, as we can no longer modify this schema once it is in use.

    // Lock the loader for read to make sure no one is concurrently loading a replacement for this
    // schema node.
    auto lock = loader.impl.lockShared();

    // Get the mutable version of the schema.
    _::RawSchema* mutableSchema = lock->get()->tryGet(schema->id).schema;
    KJ_ASSERT(mutableSchema == schema,
              "A schema not belonging to this loader used its initializer.");

    // Disable the initializer.
    __atomic_store_n(&mutableSchema->lazyInitializer, nullptr, __ATOMIC_RELEASE);
  }
}

// =======================================================================================

SchemaLoader::SchemaLoader(): impl(kj::heap<Impl>(*this)) {}
SchemaLoader::SchemaLoader(const LazyLoadCallback& callback)
    : impl(kj::heap<Impl>(*this, callback)) {}
SchemaLoader::~SchemaLoader() noexcept(false) {}

Schema SchemaLoader::get(uint64_t id) const {
  KJ_IF_MAYBE(result, tryGet(id)) {
    return *result;
  } else {
    KJ_FAIL_REQUIRE("no schema node loaded for id", id);
  }
}

kj::Maybe<Schema> SchemaLoader::tryGet(uint64_t id) const {
  auto getResult = impl.lockShared()->get()->tryGet(id);
  if (getResult.schema == nullptr || getResult.schema->lazyInitializer != nullptr) {
    KJ_IF_MAYBE(c, getResult.callback) {
      c->load(*this, id);
    }
    getResult = impl.lockShared()->get()->tryGet(id);
  }
  if (getResult.schema != nullptr && getResult.schema->lazyInitializer == nullptr) {
    return Schema(getResult.schema);
  } else {
    return nullptr;
  }
}

Schema SchemaLoader::load(const schema::Node::Reader& reader) {
  return Schema(impl.lockExclusive()->get()->load(reader, false));
}

Schema SchemaLoader::loadOnce(const schema::Node::Reader& reader) const {
  auto locked = impl.lockExclusive();
  auto getResult = locked->get()->tryGet(reader.getId());
  if (getResult.schema == nullptr || getResult.schema->lazyInitializer != nullptr) {
    // Doesn't exist yet, or the existing schema is a placeholder and therefore has not yet been
    // seen publicly.  Go ahead and load the incoming reader.
    return Schema(locked->get()->load(reader, false));
  } else {
    return Schema(getResult.schema);
  }
}

kj::Array<Schema> SchemaLoader::getAllLoaded() const {
  return impl.lockShared()->get()->getAllLoaded();
}

void SchemaLoader::loadNative(const _::RawSchema* nativeSchema) {
  impl.lockExclusive()->get()->loadNative(nativeSchema);
}

}  // namespace capnp
