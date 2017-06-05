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

#define CAPNP_PRIVATE
#include "schema-loader.h"
#include <unordered_map>
#include <unordered_set>
#include <map>
#include "message.h"
#include "arena.h"
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/arena.h>
#include <kj/vector.h>
#include <algorithm>

#if _MSC_VER
#include <atomic>
#endif

namespace capnp {

namespace {

struct ByteArrayHash {
  size_t operator()(kj::ArrayPtr<const byte> bytes) const {
    // FNV hash. Probably sucks, but the code is simple.
    //
    // TODO(perf): Add CityHash or something to KJ and use it here.

    uint64_t hash = 0xcbf29ce484222325ull;
    for (byte b: bytes) {
      hash = hash * 0x100000001b3ull;
      hash ^= b;
    }
    return hash;
  }
};

struct ByteArrayEq {
  bool operator()(kj::ArrayPtr<const byte> a, kj::ArrayPtr<const byte> b) const {
    return a.size() == b.size() && memcmp(a.begin(), b.begin(), a.size()) == 0;
  }
};

struct SchemaBindingsPair {
  const _::RawSchema* schema;
  const _::RawBrandedSchema::Scope* scopeBindings;

  inline bool operator==(const SchemaBindingsPair& other) const {
    return schema == other.schema && scopeBindings == other.scopeBindings;
  }
};

struct SchemaBindingsPairHash {
  size_t operator()(SchemaBindingsPair pair) const {
    return 31 * reinterpret_cast<uintptr_t>(pair.schema) +
                reinterpret_cast<uintptr_t>(pair.scopeBindings);
  }
};

}  // namespace

bool hasDiscriminantValue(const schema::Field::Reader& reader) {
  return reader.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT;
}

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

class SchemaLoader::BrandedInitializerImpl: public _::RawBrandedSchema::Initializer {
public:
  inline explicit BrandedInitializerImpl(const SchemaLoader& loader): loader(loader) {}

  void init(const _::RawBrandedSchema* schema) const override;

private:
  const SchemaLoader& loader;
};

class SchemaLoader::Impl {
public:
  inline explicit Impl(const SchemaLoader& loader)
      : initializer(loader), brandedInitializer(loader) {}
  inline Impl(const SchemaLoader& loader, const LazyLoadCallback& callback)
      : initializer(loader, callback), brandedInitializer(loader) {}

  _::RawSchema* load(const schema::Node::Reader& reader, bool isPlaceholder);

  _::RawSchema* loadNative(const _::RawSchema* nativeSchema);

  _::RawSchema* loadEmpty(uint64_t id, kj::StringPtr name, schema::Node::Which kind,
                          bool isPlaceholder);
  // Create a dummy empty schema of the given kind for the given id and load it.

  const _::RawBrandedSchema* makeBranded(
      const _::RawSchema* schema, schema::Brand::Reader proto,
      kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> clientBrand);

  struct TryGetResult {
    _::RawSchema* schema;
    kj::Maybe<const LazyLoadCallback&> callback;
  };

  TryGetResult tryGet(uint64_t typeId) const;

  const _::RawBrandedSchema* getUnbound(const _::RawSchema* schema);

  kj::Array<Schema> getAllLoaded() const;

  void requireStructSize(uint64_t id, uint dataWordCount, uint pointerCount);
  // Require any struct nodes loaded with this ID -- in the past and in the future -- to have at
  // least the given sizes.  Struct nodes that don't comply will simply be rewritten to comply.
  // This is used to ensure that parents of group nodes have at least the size of the group node,
  // so that allocating a struct that contains a group then getting the group node and setting
  // its fields can't possibly write outside of the allocated space.

  kj::Arena arena;

private:
  std::unordered_set<kj::ArrayPtr<const byte>, ByteArrayHash, ByteArrayEq> dedupTable;
  // Records raw segments of memory in the arena against which we my want to de-dupe later
  // additions. Specifically, RawBrandedSchema binding tables are de-duped.

  std::unordered_map<uint64_t, _::RawSchema*> schemas;
  std::unordered_map<SchemaBindingsPair, _::RawBrandedSchema*, SchemaBindingsPairHash> brands;
  std::unordered_map<const _::RawSchema*, _::RawBrandedSchema*> unboundBrands;

  struct RequiredSize {
    uint16_t dataWordCount;
    uint16_t pointerCount;
  };
  std::unordered_map<uint64_t, RequiredSize> structSizeRequirements;

  InitializerImpl initializer;
  BrandedInitializerImpl brandedInitializer;

  kj::ArrayPtr<word> makeUncheckedNode(schema::Node::Reader node);
  // Construct a copy of the given schema node, allocated as a single-segment ("unchecked") node
  // within the loader's arena.

  kj::ArrayPtr<word> makeUncheckedNodeEnforcingSizeRequirements(schema::Node::Reader node);
  // Like makeUncheckedNode() but if structSizeRequirements has a requirement for this node which
  // is larger than the node claims to be, the size will be edited to comply.  This should be rare.
  // If the incoming node is not a struct, any struct size requirements will be ignored, but if
  // such requirements exist, this indicates an inconsistency that could cause exceptions later on
  // (but at least can't cause memory corruption).

  kj::ArrayPtr<word> rewriteStructNodeWithSizes(
      schema::Node::Reader node, uint dataWordCount, uint pointerCount);
  // Make a copy of the given node (which must be a struct node) and set its sizes to be the max
  // of what it said already and the given sizes.

  // If the encoded node does not meet the given struct size requirements, make a new copy that
  // does.
  void applyStructSizeRequirement(_::RawSchema* raw, uint dataWordCount, uint pointerCount);

  const _::RawBrandedSchema* makeBranded(const _::RawSchema* schema,
      kj::ArrayPtr<const _::RawBrandedSchema::Scope> scopes);

  kj::ArrayPtr<const _::RawBrandedSchema::Dependency> makeBrandedDependencies(
      const _::RawSchema* schema,
      kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> bindings);

  void makeDep(_::RawBrandedSchema::Binding& result,
      schema::Type::Reader type, kj::StringPtr scopeName,
      kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings);
  void makeDep(_::RawBrandedSchema::Binding& result,
      uint64_t typeId, schema::Type::Which whichType, schema::Node::Which expectedKind,
      schema::Brand::Reader brand, kj::StringPtr scopeName,
      kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings);
  // Looks up the schema and brand for a dependency, or creates lazily-evaluated placeholders if
  // they don't already exist, and fills in `result`. `scopeName` is a human-readable name of the
  // place where the type appeared.
  //
  // Note that we don't simply return a Binding because we need to be careful about initialization
  // to ensure that our byte-based de-duplification works. If we constructed a Binding on the stack
  // and returned it, padding bytes in that Binding could go uninitialized, causing it to appear
  // unique when it's not. It is expected that `result` has been zero'd via memset() before these
  // methods are called.

  const _::RawBrandedSchema* makeDepSchema(
      schema::Type::Reader type, kj::StringPtr scopeName,
      kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings);
  const _::RawBrandedSchema* makeDepSchema(
      uint64_t typeId, schema::Type::Which whichType, schema::Node::Which expectedKind,
      schema::Brand::Reader brand, kj::StringPtr scopeName,
      kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings);
  // Invoke makeDep() then return the result's schema, or nullptr if it's a primitive type.

  template <typename T>
  kj::ArrayPtr<const T> copyDeduped(kj::ArrayPtr<const T> values);
  template <typename T>
  kj::ArrayPtr<const T> copyDeduped(kj::ArrayPtr<T> values);
  // Copy the given array into the arena and return the copy -- unless an identical array
  // was copied previously, in which case the existing copy is returned.

  friend class SchemaLoader::BrandedInitializerImpl;
};

// =======================================================================================

inline static void verifyVoid(Void value) {}
// Calls to this will break if the parameter type changes to non-void.  We use this to detect
// when the code needs updating.

class SchemaLoader::Validator {
public:
  Validator(SchemaLoader::Impl& loader): loader(loader) {}

  bool validate(const schema::Node::Reader& node) {
    isValid = true;
    nodeName = node.getDisplayName();
    dependencies.clear();

    KJ_CONTEXT("validating schema node", nodeName, (uint)node.which());

    if (node.getParameters().size() > 0) {
      KJ_REQUIRE(node.getIsGeneric(), "if parameter list is non-empty, isGeneric must be true") {
        isValid = false;
        return false;
      }
    }

    switch (node.which()) {
      case schema::Node::FILE:
        verifyVoid(node.getFile());
        break;
      case schema::Node::STRUCT:
        validate(node.getStruct(), node.getScopeId());
        break;
      case schema::Node::ENUM:
        validate(node.getEnum());
        break;
      case schema::Node::INTERFACE:
        validate(node.getInterface());
        break;
      case schema::Node::CONST:
        validate(node.getConst());
        break;
      case schema::Node::ANNOTATION:
        validate(node.getAnnotation());
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

  const uint16_t* makeMemberInfoArray(uint32_t* count) {
    *count = members.size();
    kj::ArrayPtr<uint16_t> result = loader.arena.allocateArray<uint16_t>(*count);
    uint pos = 0;
    for (auto& member: members) {
      result[pos++] = member.second;
    }
    KJ_DASSERT(pos == *count);
    return result.begin();
  }

  const uint16_t* makeMembersByDiscriminantArray() {
    return membersByDiscriminant.begin();
  }

private:
  SchemaLoader::Impl& loader;
  Text::Reader nodeName;
  bool isValid;
  std::map<uint64_t, _::RawSchema*> dependencies;

  // Maps name -> index for each member.
  std::map<Text::Reader, uint> members;

  kj::ArrayPtr<uint16_t> membersByDiscriminant;

#define VALIDATE_SCHEMA(condition, ...) \
  KJ_REQUIRE(condition, ##__VA_ARGS__) { isValid = false; return; }
#define FAIL_VALIDATE_SCHEMA(...) \
  KJ_FAIL_REQUIRE(__VA_ARGS__) { isValid = false; return; }

  void validateMemberName(kj::StringPtr name, uint index) {
    bool isNewName = members.insert(std::make_pair(name, index)).second;
    VALIDATE_SCHEMA(isNewName, "duplicate name", name);
  }

  void validate(const schema::Node::Struct::Reader& structNode, uint64_t scopeId) {
    uint dataSizeInBits = structNode.getDataWordCount() * 64;
    uint pointerCount = structNode.getPointerCount();

    auto fields = structNode.getFields();

    KJ_STACK_ARRAY(bool, sawCodeOrder, fields.size(), 32, 256);
    memset(sawCodeOrder.begin(), 0, sawCodeOrder.size() * sizeof(sawCodeOrder[0]));

    KJ_STACK_ARRAY(bool, sawDiscriminantValue, structNode.getDiscriminantCount(), 32, 256);
    memset(sawDiscriminantValue.begin(), 0,
           sawDiscriminantValue.size() * sizeof(sawDiscriminantValue[0]));

    if (structNode.getDiscriminantCount() > 0) {
      VALIDATE_SCHEMA(structNode.getDiscriminantCount() != 1,
                      "union must have at least two members");
      VALIDATE_SCHEMA(structNode.getDiscriminantCount() <= fields.size(),
                      "struct can't have more union fields than total fields");

      VALIDATE_SCHEMA((structNode.getDiscriminantOffset() + 1) * 16 <= dataSizeInBits,
                      "union discriminant is out-of-bounds");
    }

    membersByDiscriminant = loader.arena.allocateArray<uint16_t>(fields.size());
    uint discriminantPos = 0;
    uint nonDiscriminantPos = structNode.getDiscriminantCount();

    uint index = 0;
    uint nextOrdinal = 0;
    for (auto field: fields) {
      KJ_CONTEXT("validating struct field", field.getName());

      validateMemberName(field.getName(), index);
      VALIDATE_SCHEMA(field.getCodeOrder() < sawCodeOrder.size() &&
                      !sawCodeOrder[field.getCodeOrder()],
                      "invalid codeOrder");
      sawCodeOrder[field.getCodeOrder()] = true;

      auto ordinal = field.getOrdinal();
      if (ordinal.isExplicit()) {
        VALIDATE_SCHEMA(ordinal.getExplicit() >= nextOrdinal,
                        "fields were not ordered by ordinal");
        nextOrdinal = ordinal.getExplicit() + 1;
      }

      if (hasDiscriminantValue(field)) {
        VALIDATE_SCHEMA(field.getDiscriminantValue() < sawDiscriminantValue.size() &&
                        !sawDiscriminantValue[field.getDiscriminantValue()],
                        "invalid discriminantValue");
        sawDiscriminantValue[field.getDiscriminantValue()] = true;

        membersByDiscriminant[discriminantPos++] = index;
      } else {
        VALIDATE_SCHEMA(nonDiscriminantPos <= fields.size(),
                        "discriminantCount did not match fields");
        membersByDiscriminant[nonDiscriminantPos++] = index;
      }

      switch (field.which()) {
        case schema::Field::SLOT: {
          auto slot = field.getSlot();

          uint fieldBits = 0;
          bool fieldIsPointer = false;
          validate(slot.getType(), slot.getDefaultValue(), &fieldBits, &fieldIsPointer);
          VALIDATE_SCHEMA(fieldBits * (slot.getOffset() + 1) <= dataSizeInBits &&
                          fieldIsPointer * (slot.getOffset() + 1) <= pointerCount,
                          "field offset out-of-bounds",
                          slot.getOffset(), dataSizeInBits, pointerCount);

          break;
        }

        case schema::Field::GROUP:
          // Require that the group is a struct node.
          validateTypeId(field.getGroup().getTypeId(), schema::Node::STRUCT);
          break;
      }

      ++index;
    }

    // If the above code is correct, these should pass.
    KJ_ASSERT(discriminantPos == structNode.getDiscriminantCount());
    KJ_ASSERT(nonDiscriminantPos == fields.size());

    if (structNode.getIsGroup()) {
      VALIDATE_SCHEMA(scopeId != 0, "group node missing scopeId");

      // Require that the group's scope has at least the same size as the group, so that anyone
      // constructing an instance of the outer scope can safely read/write the group.
      loader.requireStructSize(scopeId, structNode.getDataWordCount(),
                               structNode.getPointerCount());

      // Require that the parent type is a struct.
      validateTypeId(scopeId, schema::Node::STRUCT);
    }
  }

  void validate(const schema::Node::Enum::Reader& enumNode) {
    auto enumerants = enumNode.getEnumerants();
    KJ_STACK_ARRAY(bool, sawCodeOrder, enumerants.size(), 32, 256);
    memset(sawCodeOrder.begin(), 0, sawCodeOrder.size() * sizeof(sawCodeOrder[0]));

    uint index = 0;
    for (auto enumerant: enumerants) {
      validateMemberName(enumerant.getName(), index++);

      VALIDATE_SCHEMA(enumerant.getCodeOrder() < enumerants.size() &&
                      !sawCodeOrder[enumerant.getCodeOrder()],
                      "invalid codeOrder", enumerant.getName());
      sawCodeOrder[enumerant.getCodeOrder()] = true;
    }
  }

  void validate(const schema::Node::Interface::Reader& interfaceNode) {
    for (auto extend: interfaceNode.getSuperclasses()) {
      validateTypeId(extend.getId(), schema::Node::INTERFACE);
      validate(extend.getBrand());
    }

    auto methods = interfaceNode.getMethods();
    KJ_STACK_ARRAY(bool, sawCodeOrder, methods.size(), 32, 256);
    memset(sawCodeOrder.begin(), 0, sawCodeOrder.size() * sizeof(sawCodeOrder[0]));

    uint index = 0;
    for (auto method: methods) {
      KJ_CONTEXT("validating method", method.getName());
      validateMemberName(method.getName(), index++);

      VALIDATE_SCHEMA(method.getCodeOrder() < methods.size() &&
                      !sawCodeOrder[method.getCodeOrder()],
                      "invalid codeOrder");
      sawCodeOrder[method.getCodeOrder()] = true;

      validateTypeId(method.getParamStructType(), schema::Node::STRUCT);
      validate(method.getParamBrand());
      validateTypeId(method.getResultStructType(), schema::Node::STRUCT);
      validate(method.getResultBrand());
    }
  }

  void validate(const schema::Node::Const::Reader& constNode) {
    uint dummy1;
    bool dummy2;
    validate(constNode.getType(), constNode.getValue(), &dummy1, &dummy2);
  }

  void validate(const schema::Node::Annotation::Reader& annotationNode) {
    validate(annotationNode.getType());
  }

  void validate(const schema::Type::Reader& type, const schema::Value::Reader& value,
                uint* dataSizeInBits, bool* isPointer) {
    validate(type);

    schema::Value::Which expectedValueType = schema::Value::VOID;
    bool hadCase = false;
    switch (type.which()) {
#define HANDLE_TYPE(name, bits, ptr) \
      case schema::Type::name: \
        expectedValueType = schema::Value::name; \
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
      HANDLE_TYPE(ANY_POINTER, 0, true)
#undef HANDLE_TYPE
    }

    if (hadCase) {
      VALIDATE_SCHEMA(value.which() == expectedValueType, "Value did not match type.",
                      (uint)value.which(), (uint)expectedValueType);
    }
  }

  void validate(const schema::Type::Reader& type) {
    switch (type.which()) {
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

      case schema::Type::STRUCT: {
        auto structType = type.getStruct();
        validateTypeId(structType.getTypeId(), schema::Node::STRUCT);
        validate(structType.getBrand());
        break;
      }
      case schema::Type::ENUM: {
        auto enumType = type.getEnum();
        validateTypeId(enumType.getTypeId(), schema::Node::ENUM);
        validate(enumType.getBrand());
        break;
      }
      case schema::Type::INTERFACE: {
        auto interfaceType = type.getInterface();
        validateTypeId(interfaceType.getTypeId(), schema::Node::INTERFACE);
        validate(interfaceType.getBrand());
        break;
      }

      case schema::Type::LIST:
        validate(type.getList().getElementType());
        break;
    }

    // We intentionally allow unknown types.
  }

  void validate(const schema::Brand::Reader& brand) {
    for (auto scope: brand.getScopes()) {
      switch (scope.which()) {
        case schema::Brand::Scope::BIND:
          for (auto binding: scope.getBind()) {
            switch (binding.which()) {
              case schema::Brand::Binding::UNBOUND:
                break;
              case schema::Brand::Binding::TYPE: {
                auto type = binding.getType();
                validate(type);
                bool isPointer = true;
                switch (type.which()) {
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
                    isPointer = false;
                    break;

                  case schema::Type::TEXT:
                  case schema::Type::DATA:
                  case schema::Type::ANY_POINTER:
                  case schema::Type::STRUCT:
                  case schema::Type::INTERFACE:
                  case schema::Type::LIST:
                    isPointer = true;
                    break;
                }
                VALIDATE_SCHEMA(isPointer,
                    "generic type parameter must be a pointer type", type);

                break;
              }
            }
          }
          break;
        case schema::Brand::Scope::INHERIT:
          break;
      }
    }
  }

  void validateTypeId(uint64_t id, schema::Node::Which expectedKind) {
    _::RawSchema* existing = loader.tryGet(id).schema;
    if (existing != nullptr) {
      auto node = readMessageUnchecked<schema::Node>(existing->encodedNode);
      VALIDATE_SCHEMA(node.which() == expectedKind,
          "expected a different kind of node for this ID",
          id, (uint)expectedKind, (uint)node.which(), node.getDisplayName());
      dependencies.insert(std::make_pair(id, existing));
      return;
    }

    dependencies.insert(std::make_pair(id, loader.loadEmpty(
        id, kj::str("(unknown type used by ", nodeName , ")"), expectedKind, true)));
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
    this->existingNode = existingNode;
    this->replacementNode = replacement;

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
  schema::Node::Reader existingNode;
  schema::Node::Reader replacementNode;

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

    VALIDATE_SCHEMA(node.which() == replacement.which(),
                    "kind of declaration changed");

    // No need to check compatibility of most of the non-body parts of the node:
    // - Arbitrary renaming and moving between scopes is allowed.
    // - Annotations are ignored for compatibility purposes.

    if (replacement.getParameters().size() > node.getParameters().size()) {
      replacementIsNewer();
    } else if (replacement.getParameters().size() < node.getParameters().size()) {
      replacementIsOlder();
    }

    switch (node.which()) {
      case schema::Node::FILE:
        verifyVoid(node.getFile());
        break;
      case schema::Node::STRUCT:
        checkCompatibility(node.getStruct(), replacement.getStruct(),
                           node.getScopeId(), replacement.getScopeId());
        break;
      case schema::Node::ENUM:
        checkCompatibility(node.getEnum(), replacement.getEnum());
        break;
      case schema::Node::INTERFACE:
        checkCompatibility(node.getInterface(), replacement.getInterface());
        break;
      case schema::Node::CONST:
        checkCompatibility(node.getConst(), replacement.getConst());
        break;
      case schema::Node::ANNOTATION:
        checkCompatibility(node.getAnnotation(), replacement.getAnnotation());
        break;
    }
  }

  void checkCompatibility(const schema::Node::Struct::Reader& structNode,
                          const schema::Node::Struct::Reader& replacement,
                          uint64_t scopeId, uint64_t replacementScopeId) {
    if (replacement.getDataWordCount() > structNode.getDataWordCount()) {
      replacementIsNewer();
    } else if (replacement.getDataWordCount() < structNode.getDataWordCount()) {
      replacementIsOlder();
    }
    if (replacement.getPointerCount() > structNode.getPointerCount()) {
      replacementIsNewer();
    } else if (replacement.getPointerCount() < structNode.getPointerCount()) {
      replacementIsOlder();
    }
    if (replacement.getDiscriminantCount() > structNode.getDiscriminantCount()) {
      replacementIsNewer();
    } else if (replacement.getDiscriminantCount() < structNode.getDiscriminantCount()) {
      replacementIsOlder();
    }

    if (replacement.getDiscriminantCount() > 0 && structNode.getDiscriminantCount() > 0) {
      VALIDATE_SCHEMA(replacement.getDiscriminantOffset() == structNode.getDiscriminantOffset(),
                      "union discriminant position changed");
    }

    // The shared members should occupy corresponding positions in the member lists, since the
    // lists are sorted by ordinal.
    auto fields = structNode.getFields();
    auto replacementFields = replacement.getFields();
    uint count = std::min(fields.size(), replacementFields.size());

    if (replacementFields.size() > fields.size()) {
      replacementIsNewer();
    } else if (replacementFields.size() < fields.size()) {
      replacementIsOlder();
    }

    for (uint i = 0; i < count; i++) {
      checkCompatibility(fields[i], replacementFields[i]);
    }

    // For the moment, we allow "upgrading" from non-group to group, mainly so that the
    // placeholders we generate for group parents (which in the absence of more info, we assume to
    // be non-groups) can be replaced with groups.
    //
    // TODO(cleanup):  The placeholder approach is really breaking down.  Maybe we need to maintain
    //   a list of expectations for nodes we haven't loaded yet.
    if (structNode.getIsGroup()) {
      if (replacement.getIsGroup()) {
        VALIDATE_SCHEMA(replacementScopeId == scopeId, "group node's scope changed");
      } else {
        replacementIsOlder();
      }
    } else {
      if (replacement.getIsGroup()) {
        replacementIsNewer();
      }
    }
  }

  void checkCompatibility(const schema::Field::Reader& field,
                          const schema::Field::Reader& replacement) {
    KJ_CONTEXT("comparing struct field", field.getName());

    // A field that is initially not in a union can be upgraded to be in one, as long as it has
    // discriminant 0.
    uint discriminant = hasDiscriminantValue(field) ? field.getDiscriminantValue() : 0;
    uint replacementDiscriminant =
        hasDiscriminantValue(replacement) ? replacement.getDiscriminantValue() : 0;
    VALIDATE_SCHEMA(discriminant == replacementDiscriminant, "Field discriminant changed.");

    switch (field.which()) {
      case schema::Field::SLOT: {
        auto slot = field.getSlot();

        switch (replacement.which()) {
          case schema::Field::SLOT: {
            auto replacementSlot = replacement.getSlot();

            checkCompatibility(slot.getType(), replacementSlot.getType(),
                               NO_UPGRADE_TO_STRUCT);
            checkDefaultCompatibility(slot.getDefaultValue(),
                                      replacementSlot.getDefaultValue());

            VALIDATE_SCHEMA(slot.getOffset() == replacementSlot.getOffset(),
                            "field position changed");
            break;
          }
          case schema::Field::GROUP:
            checkUpgradeToStruct(slot.getType(), replacement.getGroup().getTypeId(),
                                 existingNode, field);
            break;
        }

        break;
      }

      case schema::Field::GROUP:
        switch (replacement.which()) {
          case schema::Field::SLOT:
            checkUpgradeToStruct(replacement.getSlot().getType(), field.getGroup().getTypeId(),
                                 replacementNode, replacement);
            break;
          case schema::Field::GROUP:
            VALIDATE_SCHEMA(field.getGroup().getTypeId() == replacement.getGroup().getTypeId(),
                            "group id changed");
            break;
        }
        break;
    }
  }

  void checkCompatibility(const schema::Node::Enum::Reader& enumNode,
                          const schema::Node::Enum::Reader& replacement) {
    uint size = enumNode.getEnumerants().size();
    uint replacementSize = replacement.getEnumerants().size();
    if (replacementSize > size) {
      replacementIsNewer();
    } else if (replacementSize < size) {
      replacementIsOlder();
    }
  }

  void checkCompatibility(const schema::Node::Interface::Reader& interfaceNode,
                          const schema::Node::Interface::Reader& replacement) {
    {
      // Check superclasses.

      kj::Vector<uint64_t> superclasses;
      kj::Vector<uint64_t> replacementSuperclasses;
      for (auto superclass: interfaceNode.getSuperclasses()) {
        superclasses.add(superclass.getId());
      }
      for (auto superclass: replacement.getSuperclasses()) {
        replacementSuperclasses.add(superclass.getId());
      }
      std::sort(superclasses.begin(), superclasses.end());
      std::sort(replacementSuperclasses.begin(), replacementSuperclasses.end());

      auto iter = superclasses.begin();
      auto replacementIter = replacementSuperclasses.begin();

      while (iter != superclasses.end() || replacementIter != replacementSuperclasses.end()) {
        if (iter == superclasses.end()) {
          replacementIsNewer();
          break;
        } else if (replacementIter == replacementSuperclasses.end()) {
          replacementIsOlder();
          break;
        } else if (*iter < *replacementIter) {
          replacementIsOlder();
          ++iter;
        } else if (*iter > *replacementIter) {
          replacementIsNewer();
          ++replacementIter;
        } else {
          ++iter;
          ++replacementIter;
        }
      }
    }

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

  void checkCompatibility(const schema::Method::Reader& method,
                          const schema::Method::Reader& replacement) {
    KJ_CONTEXT("comparing method", method.getName());

    // TODO(someday):  Allow named parameter list to be replaced by compatible struct type.
    VALIDATE_SCHEMA(method.getParamStructType() == replacement.getParamStructType(),
                    "Updated method has different parameters.");
    VALIDATE_SCHEMA(method.getResultStructType() == replacement.getResultStructType(),
                    "Updated method has different results.");
  }

  void checkCompatibility(const schema::Node::Const::Reader& constNode,
                          const schema::Node::Const::Reader& replacement) {
    // Who cares?  These don't appear on the wire.
  }

  void checkCompatibility(const schema::Node::Annotation::Reader& annotationNode,
                          const schema::Node::Annotation::Reader& replacement) {
    // Who cares?  These don't appear on the wire.
  }

  enum UpgradeToStructMode {
    ALLOW_UPGRADE_TO_STRUCT,
    NO_UPGRADE_TO_STRUCT
  };

  void checkCompatibility(const schema::Type::Reader& type,
                          const schema::Type::Reader& replacement,
                          UpgradeToStructMode upgradeToStructMode) {
    if (replacement.which() != type.which()) {
      // Check for allowed "upgrade" to Data or AnyPointer.
      if (replacement.isData() && canUpgradeToData(type)) {
        replacementIsNewer();
        return;
      } else if (type.isData() && canUpgradeToData(replacement)) {
        replacementIsOlder();
        return;
      } else if (replacement.isAnyPointer() && canUpgradeToAnyPointer(type)) {
        replacementIsNewer();
        return;
      } else if (type.isAnyPointer() && canUpgradeToAnyPointer(replacement)) {
        replacementIsOlder();
        return;
      }

      if (upgradeToStructMode == ALLOW_UPGRADE_TO_STRUCT) {
        if (type.isStruct()) {
          checkUpgradeToStruct(replacement, type.getStruct().getTypeId());
          return;
        } else if (replacement.isStruct()) {
          checkUpgradeToStruct(type, replacement.getStruct().getTypeId());
          return;
        }
      }

      FAIL_VALIDATE_SCHEMA("a type was changed");
    }

    switch (type.which()) {
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
        return;

      case schema::Type::LIST:
        checkCompatibility(type.getList().getElementType(), replacement.getList().getElementType(),
                           ALLOW_UPGRADE_TO_STRUCT);
        return;

      case schema::Type::ENUM:
        VALIDATE_SCHEMA(replacement.getEnum().getTypeId() == type.getEnum().getTypeId(),
                        "type changed enum type");
        return;

      case schema::Type::STRUCT:
        // TODO(someday):  If the IDs don't match, we should compare the two structs for
        //   compatibility.  This is tricky, though, because the new type's target may not yet be
        //   loaded.  In that case we could take the old type, make a copy of it, assign the new
        //   ID to the copy, and load() that.  That forces any struct type loaded for that ID to
        //   be compatible.  However, that has another problem, which is that it could be that the
        //   whole reason the type was replaced was to fork that type, and so an incompatibility
        //   could be very much expected.  This could be a rat hole...
        VALIDATE_SCHEMA(replacement.getStruct().getTypeId() == type.getStruct().getTypeId(),
                        "type changed to incompatible struct type");
        return;

      case schema::Type::INTERFACE:
        VALIDATE_SCHEMA(replacement.getInterface().getTypeId() == type.getInterface().getTypeId(),
                        "type changed to incompatible interface type");
        return;
    }

    // We assume unknown types (from newer versions of Cap'n Proto?) are equivalent.
  }

  void checkUpgradeToStruct(const schema::Type::Reader& type, uint64_t structTypeId,
                            kj::Maybe<schema::Node::Reader> matchSize = nullptr,
                            kj::Maybe<schema::Field::Reader> matchPosition = nullptr) {
    // We can't just look up the target struct and check it because it may not have been loaded
    // yet.  Instead, we contrive a struct that looks like what we want and load() that, which
    // guarantees that any incompatibility will be caught either now or when the real version of
    // that struct is loaded.

    word scratch[32];
    memset(scratch, 0, sizeof(scratch));
    MallocMessageBuilder builder(scratch);
    auto node = builder.initRoot<schema::Node>();
    node.setId(structTypeId);
    node.setDisplayName(kj::str("(unknown type used in ", nodeName, ")"));
    auto structNode = node.initStruct();

    switch (type.which()) {
      case schema::Type::VOID:
        structNode.setDataWordCount(0);
        structNode.setPointerCount(0);
        break;

      case schema::Type::BOOL:
        structNode.setDataWordCount(1);
        structNode.setPointerCount(0);
        break;

      case schema::Type::INT8:
      case schema::Type::UINT8:
        structNode.setDataWordCount(1);
        structNode.setPointerCount(0);
        break;

      case schema::Type::INT16:
      case schema::Type::UINT16:
      case schema::Type::ENUM:
        structNode.setDataWordCount(1);
        structNode.setPointerCount(0);
        break;

      case schema::Type::INT32:
      case schema::Type::UINT32:
      case schema::Type::FLOAT32:
        structNode.setDataWordCount(1);
        structNode.setPointerCount(0);
        break;

      case schema::Type::INT64:
      case schema::Type::UINT64:
      case schema::Type::FLOAT64:
        structNode.setDataWordCount(1);
        structNode.setPointerCount(0);
        break;

      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        structNode.setDataWordCount(0);
        structNode.setPointerCount(1);
        break;
    }

    KJ_IF_MAYBE(s, matchSize) {
      auto match = s->getStruct();
      structNode.setDataWordCount(match.getDataWordCount());
      structNode.setPointerCount(match.getPointerCount());
    }

    auto field = structNode.initFields(1)[0];
    field.setName("member0");
    field.setCodeOrder(0);
    auto slot = field.initSlot();
    slot.setType(type);

    KJ_IF_MAYBE(p, matchPosition) {
      if (p->getOrdinal().isExplicit()) {
        field.getOrdinal().setExplicit(p->getOrdinal().getExplicit());
      } else {
        field.getOrdinal().setImplicit();
      }
      auto matchSlot = p->getSlot();
      slot.setOffset(matchSlot.getOffset());
      slot.setDefaultValue(matchSlot.getDefaultValue());
    } else {
      field.getOrdinal().setExplicit(0);
      slot.setOffset(0);

      schema::Value::Builder value = slot.initDefaultValue();
      switch (type.which()) {
        case schema::Type::VOID: value.setVoid(); break;
        case schema::Type::BOOL: value.setBool(false); break;
        case schema::Type::INT8: value.setInt8(0); break;
        case schema::Type::INT16: value.setInt16(0); break;
        case schema::Type::INT32: value.setInt32(0); break;
        case schema::Type::INT64: value.setInt64(0); break;
        case schema::Type::UINT8: value.setUint8(0); break;
        case schema::Type::UINT16: value.setUint16(0); break;
        case schema::Type::UINT32: value.setUint32(0); break;
        case schema::Type::UINT64: value.setUint64(0); break;
        case schema::Type::FLOAT32: value.setFloat32(0); break;
        case schema::Type::FLOAT64: value.setFloat64(0); break;
        case schema::Type::ENUM: value.setEnum(0); break;
        case schema::Type::TEXT: value.adoptText(Orphan<Text>()); break;
        case schema::Type::DATA: value.adoptData(Orphan<Data>()); break;
        case schema::Type::LIST: value.initList(); break;
        case schema::Type::STRUCT: value.initStruct(); break;
        case schema::Type::INTERFACE: value.setInterface(); break;
        case schema::Type::ANY_POINTER: value.initAnyPointer(); break;
      }
    }

    loader.load(node, true);
  }

  bool canUpgradeToData(const schema::Type::Reader& type) {
    if (type.isText()) {
      return true;
    } else if (type.isList()) {
      switch (type.getList().getElementType().which()) {
        case schema::Type::INT8:
        case schema::Type::UINT8:
          return true;
        default:
          return false;
      }
    } else {
      return false;
    }
  }

  bool canUpgradeToAnyPointer(const schema::Type::Reader& type) {
    switch (type.which()) {
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
        return false;

      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        return true;
    }

    // Be lenient with unknown types.
    return true;
  }

  void checkDefaultCompatibility(const schema::Value::Reader& value,
                                 const schema::Value::Reader& replacement) {
    // Note that we test default compatibility only after testing type compatibility, and default
    // values have already been validated as matching their types, so this should pass.
    KJ_ASSERT(value.which() == replacement.which()) {
      compatibility = INCOMPATIBLE;
      return;
    }

    switch (value.which()) {
#define HANDLE_TYPE(discrim, name) \
      case schema::Value::discrim: \
        VALIDATE_SCHEMA(value.get##name() == replacement.get##name(), "default value changed"); \
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

      case schema::Value::TEXT:
      case schema::Value::DATA:
      case schema::Value::LIST:
      case schema::Value::STRUCT:
      case schema::Value::INTERFACE:
      case schema::Value::ANY_POINTER:
        // It's not a big deal if default values for pointers change, and it would be difficult for
        // us to compare these defaults here, so just let it slide.
        break;
    }
  }
};

// =======================================================================================

_::RawSchema* SchemaLoader::Impl::load(const schema::Node::Reader& reader, bool isPlaceholder) {
  // Make a copy of the node which can be used unchecked.
  kj::ArrayPtr<word> validated = makeUncheckedNodeEnforcingSizeRequirements(reader);

  // Validate the copy.
  Validator validator(*this);
  auto validatedReader = readMessageUnchecked<schema::Node>(validated.begin());

  if (!validator.validate(validatedReader)) {
    // Not valid.  Construct an empty schema of the same type and return that.
    return loadEmpty(validatedReader.getId(),
                     validatedReader.getDisplayName(),
                     validatedReader.which(),
                     false);
  }

  // Check if we already have a schema for this ID.
  _::RawSchema*& slot = schemas[validatedReader.getId()];
  bool shouldReplace;
  bool shouldClearInitializer;
  if (slot == nullptr) {
    // Nope, allocate a new RawSchema.
    slot = &arena.allocate<_::RawSchema>();
    memset(&slot->defaultBrand, 0, sizeof(slot->defaultBrand));
    slot->id = validatedReader.getId();
    slot->canCastTo = nullptr;
    slot->defaultBrand.generic = slot;
    slot->lazyInitializer = isPlaceholder ? &initializer : nullptr;
    slot->defaultBrand.lazyInitializer = isPlaceholder ? &brandedInitializer : nullptr;
    shouldReplace = true;
    shouldClearInitializer = false;
  } else {
    // Yes, check if it is compatible and figure out which schema is newer.

    // If the existing slot is a placeholder, but we're upgrading it to a non-placeholder, we
    // need to clear the initializer later.
    shouldClearInitializer = slot->lazyInitializer != nullptr && !isPlaceholder;

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
    slot->encodedSize = validated.size();
    slot->dependencies = validator.makeDependencyArray(&slot->dependencyCount);
    slot->membersByName = validator.makeMemberInfoArray(&slot->memberCount);
    slot->membersByDiscriminant = validator.makeMembersByDiscriminantArray();

    // Even though this schema isn't itself branded, it may have dependencies that are. So, we
    // need to set up the "dependencies" map under defaultBrand.
    auto deps = makeBrandedDependencies(slot, kj::ArrayPtr<const _::RawBrandedSchema::Scope>());
    slot->defaultBrand.dependencies = deps.begin();
    slot->defaultBrand.dependencyCount = deps.size();
  }

  if (shouldClearInitializer) {
    // If this schema is not newly-allocated, it may already be in the wild, specifically in the
    // dependency list of other schemas.  Once the initializer is null, it is live, so we must do
    // a release-store here.
#if __GNUC__
    __atomic_store_n(&slot->lazyInitializer, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&slot->defaultBrand.lazyInitializer, nullptr, __ATOMIC_RELEASE);
#elif _MSC_VER
    std::atomic_thread_fence(std::memory_order_release);
    *static_cast<_::RawSchema::Initializer const* volatile*>(&slot->lazyInitializer) = nullptr;
    *static_cast<_::RawBrandedSchema::Initializer const* volatile*>(
        &slot->defaultBrand.lazyInitializer) = nullptr;
#else
#error "Platform not supported"
#endif
  }

  return slot;
}

_::RawSchema* SchemaLoader::Impl::loadNative(const _::RawSchema* nativeSchema) {
  _::RawSchema*& slot = schemas[nativeSchema->id];
  bool shouldReplace;
  bool shouldClearInitializer;
  if (slot == nullptr) {
    slot = &arena.allocate<_::RawSchema>();
    memset(&slot->defaultBrand, 0, sizeof(slot->defaultBrand));
    slot->defaultBrand.generic = slot;
    slot->lazyInitializer = nullptr;
    slot->defaultBrand.lazyInitializer = nullptr;
    shouldReplace = true;
    shouldClearInitializer = false;  // already cleared above
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
    shouldClearInitializer = slot->lazyInitializer != nullptr;
  }

  // Since we recurse below, the slot in the hash map could move around.  Copy out the pointer
  // for subsequent use.
  // TODO(cleanup): Above comment is actually not true of unordered_map. Leaving here to explain
  //   code pattern below.
  _::RawSchema* result = slot;

  if (shouldReplace) {
    // Set the schema to a copy of the native schema, but make sure not to null out lazyInitializer
    // yet.
    _::RawSchema temp = *nativeSchema;
    temp.lazyInitializer = result->lazyInitializer;
    *result = temp;

    result->defaultBrand.generic = result;

    // Indicate that casting is safe.  Note that it's important to set this before recursively
    // loading dependencies, so that cycles don't cause infinite loops!
    result->canCastTo = nativeSchema;

    // We need to set the dependency list to point at other loader-owned RawSchemas.
    kj::ArrayPtr<const _::RawSchema*> dependencies =
        arena.allocateArray<const _::RawSchema*>(result->dependencyCount);
    for (uint i = 0; i < nativeSchema->dependencyCount; i++) {
      dependencies[i] = loadNative(nativeSchema->dependencies[i]);
    }
    result->dependencies = dependencies.begin();

    // Also need to re-do the branded dependencies.
    auto deps = makeBrandedDependencies(slot, kj::ArrayPtr<const _::RawBrandedSchema::Scope>());
    slot->defaultBrand.dependencies = deps.begin();
    slot->defaultBrand.dependencyCount = deps.size();

    // If there is a struct size requirement, we need to make sure that it is satisfied.
    auto reqIter = structSizeRequirements.find(nativeSchema->id);
    if (reqIter != structSizeRequirements.end()) {
      applyStructSizeRequirement(result, reqIter->second.dataWordCount,
                                 reqIter->second.pointerCount);
    }
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

  if (shouldClearInitializer) {
    // If this schema is not newly-allocated, it may already be in the wild, specifically in the
    // dependency list of other schemas.  Once the initializer is null, it is live, so we must do
    // a release-store here.
#if __GNUC__
    __atomic_store_n(&result->lazyInitializer, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&result->defaultBrand.lazyInitializer, nullptr, __ATOMIC_RELEASE);
#elif _MSC_VER
    std::atomic_thread_fence(std::memory_order_release);
    *static_cast<_::RawSchema::Initializer const* volatile*>(&result->lazyInitializer) = nullptr;
    *static_cast<_::RawBrandedSchema::Initializer const* volatile*>(
        &result->defaultBrand.lazyInitializer) = nullptr;
#else
#error "Platform not supported"
#endif
  }

  return result;
}

_::RawSchema* SchemaLoader::Impl::loadEmpty(
    uint64_t id, kj::StringPtr name, schema::Node::Which kind, bool isPlaceholder) {
  word scratch[32];
  memset(scratch, 0, sizeof(scratch));
  MallocMessageBuilder builder(scratch);
  auto node = builder.initRoot<schema::Node>();
  node.setId(id);
  node.setDisplayName(name);
  switch (kind) {
    case schema::Node::STRUCT: node.initStruct(); break;
    case schema::Node::ENUM: node.initEnum(); break;
    case schema::Node::INTERFACE: node.initInterface(); break;

    case schema::Node::FILE:
    case schema::Node::CONST:
    case schema::Node::ANNOTATION:
      KJ_FAIL_REQUIRE("Not a type.");
      break;
  }

  return load(node, isPlaceholder);
}

const _::RawBrandedSchema* SchemaLoader::Impl::makeBranded(
    const _::RawSchema* schema, schema::Brand::Reader proto,
    kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> clientBrand) {
  kj::StringPtr scopeName =
      readMessageUnchecked<schema::Node>(schema->encodedNode).getDisplayName();

  auto srcScopes = proto.getScopes();

  KJ_STACK_ARRAY(_::RawBrandedSchema::Scope, dstScopes, srcScopes.size(), 16, 32);
  memset(dstScopes.begin(), 0, dstScopes.size() * sizeof(dstScopes[0]));

  uint dstScopeCount = 0;
  for (auto srcScope: srcScopes) {
    switch (srcScope.which()) {
      case schema::Brand::Scope::BIND: {
        auto srcBindings = srcScope.getBind();
        KJ_STACK_ARRAY(_::RawBrandedSchema::Binding, dstBindings, srcBindings.size(), 16, 32);
        memset(dstBindings.begin(), 0, dstBindings.size() * sizeof(dstBindings[0]));

        for (auto j: kj::indices(srcBindings)) {
          auto srcBinding = srcBindings[j];
          auto& dstBinding = dstBindings[j];

          memset(&dstBinding, 0, sizeof(dstBinding));
          dstBinding.which = schema::Type::ANY_POINTER;

          switch (srcBinding.which()) {
            case schema::Brand::Binding::UNBOUND:
              break;
            case schema::Brand::Binding::TYPE: {
              makeDep(dstBinding, srcBinding.getType(), scopeName, clientBrand);
              break;
            }
          }
        }

        auto& dstScope = dstScopes[dstScopeCount++];
        dstScope.typeId = srcScope.getScopeId();
        dstScope.bindingCount = dstBindings.size();
        dstScope.bindings = copyDeduped(dstBindings).begin();
        break;
      }
      case schema::Brand::Scope::INHERIT: {
        // Inherit the whole scope from the client -- or if the client doesn't have it, at least
        // include an empty dstScope in the list just to show that this scope was specified as
        // inherited, as opposed to being unspecified (which would be treated as all AnyPointer).
        auto& dstScope = dstScopes[dstScopeCount++];
        dstScope.typeId = srcScope.getScopeId();

        KJ_IF_MAYBE(b, clientBrand) {
          for (auto& clientScope: *b) {
            if (clientScope.typeId == dstScope.typeId) {
              // Overwrite the whole thing.
              dstScope = clientScope;
              break;
            }
          }
        } else {
          dstScope.isUnbound = true;
        }
        break;
      }
    }
  }

  dstScopes = dstScopes.slice(0, dstScopeCount);

  std::sort(dstScopes.begin(), dstScopes.end(),
      [](const _::RawBrandedSchema::Scope& a, const _::RawBrandedSchema::Scope& b) {
    return a.typeId < b.typeId;
  });

  return makeBranded(schema, copyDeduped(dstScopes));
}

const _::RawBrandedSchema* SchemaLoader::Impl::makeBranded(
    const _::RawSchema* schema, kj::ArrayPtr<const _::RawBrandedSchema::Scope> bindings) {
  // Note that even if `bindings` is empty, we never want to return defaultBrand here because
  // defaultBrand has special status. Normally, the lack of bindings means all parameters are
  // "unspecified", which means their bindings are unknown and should be treated as AnyPointer.
  // But defaultBrand represents a special case where all parameters are still parameters -- they
  // haven't been bound in the first place. defaultBrand is used to represent the unbranded generic
  // type, while a no-binding brand is equivalent to binding all parameters to AnyPointer.

  if (bindings.size() == 0) {
    return &schema->defaultBrand;
  }

  auto& slot = brands[SchemaBindingsPair { schema, bindings.begin() }];

  if (slot == nullptr) {
    auto& brand = arena.allocate<_::RawBrandedSchema>();
    memset(&brand, 0, sizeof(brand));
    slot = &brand;

    brand.generic = schema;
    brand.scopes = bindings.begin();
    brand.scopeCount = bindings.size();
    brand.lazyInitializer = &brandedInitializer;
  }

  return slot;
}

kj::ArrayPtr<const _::RawBrandedSchema::Dependency>
SchemaLoader::Impl::makeBrandedDependencies(
    const _::RawSchema* schema,
    kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> bindings) {
  kj::StringPtr scopeName =
      readMessageUnchecked<schema::Node>(schema->encodedNode).getDisplayName();

  kj::Vector<_::RawBrandedSchema::Dependency> deps;

  schema::Node::Reader node = readMessageUnchecked<schema::Node>(schema->encodedNode);

#define ADD_ENTRY(kind, index, make) \
    if (const _::RawBrandedSchema* dep = make) { \
      auto& slot = deps.add(); \
      memset(&slot, 0, sizeof(slot)); \
      slot.location = _::RawBrandedSchema::makeDepLocation( \
        _::RawBrandedSchema::DepKind::kind, index); \
      slot.schema = dep; \
    }

  switch (node.which()) {
    case schema::Node::FILE:
    case schema::Node::ENUM:
    case schema::Node::ANNOTATION:
      break;

    case schema::Node::CONST:
      ADD_ENTRY(CONST_TYPE, 0, makeDepSchema(
          node.getConst().getType(), scopeName, bindings));
      break;

    case schema::Node::STRUCT: {
      auto fields = node.getStruct().getFields();
      for (auto i: kj::indices(fields)) {
        auto field = fields[i];
        switch (field.which()) {
          case schema::Field::SLOT:
            ADD_ENTRY(FIELD, i, makeDepSchema(
                field.getSlot().getType(), scopeName, bindings))
            break;
          case schema::Field::GROUP: {
            const _::RawSchema* group = loadEmpty(
                field.getGroup().getTypeId(),
                "(unknown group type)", schema::Node::STRUCT, true);
            KJ_IF_MAYBE(b, bindings) {
              ADD_ENTRY(FIELD, i, makeBranded(group, *b));
            } else {
              ADD_ENTRY(FIELD, i, getUnbound(group));
            }
            break;
          }
        }
      }
      break;
    }

    case schema::Node::INTERFACE: {
      auto interface = node.getInterface();
      {
        auto superclasses = interface.getSuperclasses();
        for (auto i: kj::indices(superclasses)) {
          auto superclass = superclasses[i];
          ADD_ENTRY(SUPERCLASS, i, makeDepSchema(
              superclass.getId(), schema::Type::INTERFACE, schema::Node::INTERFACE,
              superclass.getBrand(), scopeName, bindings))
        }
      }
      {
        auto methods = interface.getMethods();
        for (auto i: kj::indices(methods)) {
          auto method = methods[i];
          ADD_ENTRY(METHOD_PARAMS, i, makeDepSchema(
              method.getParamStructType(), schema::Type::STRUCT, schema::Node::STRUCT,
              method.getParamBrand(), scopeName, bindings))
          ADD_ENTRY(METHOD_RESULTS, i, makeDepSchema(
              method.getResultStructType(), schema::Type::STRUCT, schema::Node::STRUCT,
              method.getResultBrand(), scopeName, bindings))
        }
      }
      break;
    }
  }

#undef ADD_ENTRY

  std::sort(deps.begin(), deps.end(),
      [](const _::RawBrandedSchema::Dependency& a, const _::RawBrandedSchema::Dependency& b) {
    return a.location < b.location;
  });

  return copyDeduped(deps.asPtr());
}

void SchemaLoader::Impl::makeDep(_::RawBrandedSchema::Binding& result,
    schema::Type::Reader type, kj::StringPtr scopeName,
    kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings) {
  switch (type.which()) {
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
      result.which = static_cast<uint8_t>(type.which());
      return;

    case schema::Type::STRUCT: {
      auto structType = type.getStruct();
      makeDep(result, structType.getTypeId(), schema::Type::STRUCT, schema::Node::STRUCT,
              structType.getBrand(), scopeName, brandBindings);
      return;
    }
    case schema::Type::ENUM: {
      auto enumType = type.getEnum();
      makeDep(result, enumType.getTypeId(), schema::Type::ENUM, schema::Node::ENUM,
              enumType.getBrand(), scopeName, brandBindings);
      return;
    }
    case schema::Type::INTERFACE: {
      auto interfaceType = type.getInterface();
      makeDep(result, interfaceType.getTypeId(), schema::Type::INTERFACE, schema::Node::INTERFACE,
              interfaceType.getBrand(), scopeName, brandBindings);
      return;
    }

    case schema::Type::LIST: {
      makeDep(result, type.getList().getElementType(), scopeName, brandBindings);
      ++result.listDepth;
      return;
    }

    case schema::Type::ANY_POINTER: {
      result.which = static_cast<uint8_t>(schema::Type::ANY_POINTER);
      auto anyPointer = type.getAnyPointer();
      switch (anyPointer.which()) {
        case schema::Type::AnyPointer::UNCONSTRAINED:
          return;
        case schema::Type::AnyPointer::PARAMETER: {
          auto param = anyPointer.getParameter();
          uint64_t id = param.getScopeId();
          uint16_t index = param.getParameterIndex();

          KJ_IF_MAYBE(b, brandBindings) {
            // TODO(perf): We could binary search here, but... bleh.
            for (auto& scope: *b) {
              if (scope.typeId == id) {
                if (scope.isUnbound) {
                  // Unbound brand parameter.
                  result.scopeId = id;
                  result.paramIndex = index;
                  return;
                } else if (index >= scope.bindingCount) {
                  // Binding index out-of-range. Treat as AnyPointer. This is important to allow
                  // new type parameters to be added to existing types without breaking dependent
                  // schemas.
                  return;
                } else {
                  result = scope.bindings[index];
                  return;
                }
              }
            }
            return;
          } else {
            // Unbound brand parameter.
            result.scopeId = id;
            result.paramIndex = index;
            return;
          }
        }
        case schema::Type::AnyPointer::IMPLICIT_METHOD_PARAMETER:
          result.isImplicitParameter = true;
          result.paramIndex = anyPointer.getImplicitMethodParameter().getParameterIndex();
          return;
      }
      KJ_UNREACHABLE;
    }
  }

  KJ_UNREACHABLE;
}

void SchemaLoader::Impl::makeDep(_::RawBrandedSchema::Binding& result,
    uint64_t typeId, schema::Type::Which whichType, schema::Node::Which expectedKind,
    schema::Brand::Reader brand, kj::StringPtr scopeName,
    kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings) {
  const _::RawSchema* schema = loadEmpty(typeId,
      kj::str("(unknown type; seen as dependency of ", scopeName, ")"),
      expectedKind, true);
  result.which = static_cast<uint8_t>(whichType);
  result.schema = makeBranded(schema, brand, brandBindings);
}

const _::RawBrandedSchema* SchemaLoader::Impl::makeDepSchema(
    schema::Type::Reader type, kj::StringPtr scopeName,
    kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings) {
  _::RawBrandedSchema::Binding binding;
  memset(&binding, 0, sizeof(binding));
  makeDep(binding, type, scopeName, brandBindings);
  return binding.schema;
}

const _::RawBrandedSchema* SchemaLoader::Impl::makeDepSchema(
    uint64_t typeId, schema::Type::Which whichType, schema::Node::Which expectedKind,
    schema::Brand::Reader brand, kj::StringPtr scopeName,
    kj::Maybe<kj::ArrayPtr<const _::RawBrandedSchema::Scope>> brandBindings) {
  _::RawBrandedSchema::Binding binding;
  memset(&binding, 0, sizeof(binding));
  makeDep(binding, typeId, whichType, expectedKind, brand, scopeName, brandBindings);
  return binding.schema;
}

template <typename T>
kj::ArrayPtr<const T> SchemaLoader::Impl::copyDeduped(kj::ArrayPtr<const T> values) {
  if (values.size() == 0) {
    return kj::arrayPtr(kj::implicitCast<const T*>(nullptr), 0);
  }

  auto bytes = values.asBytes();

  auto iter = dedupTable.find(bytes);
  if (iter != dedupTable.end()) {
    return kj::arrayPtr(reinterpret_cast<const T*>(iter->begin()), values.size());
  }

  // Need to make a new copy.
  auto copy = arena.allocateArray<T>(values.size());
  memcpy(copy.begin(), values.begin(), values.size() * sizeof(T));

  KJ_ASSERT(dedupTable.insert(copy.asBytes()).second);

  return copy;
}

template <typename T>
kj::ArrayPtr<const T> SchemaLoader::Impl::copyDeduped(kj::ArrayPtr<T> values) {
  return copyDeduped(kj::ArrayPtr<const T>(values));
}

SchemaLoader::Impl::TryGetResult SchemaLoader::Impl::tryGet(uint64_t typeId) const {
  auto iter = schemas.find(typeId);
  if (iter == schemas.end()) {
    return {nullptr, initializer.getCallback()};
  } else {
    return {iter->second, initializer.getCallback()};
  }
}

const _::RawBrandedSchema* SchemaLoader::Impl::getUnbound(const _::RawSchema* schema) {
  if (!readMessageUnchecked<schema::Node>(schema->encodedNode).getIsGeneric()) {
    // Not a generic type, so just return the default brand.
    return &schema->defaultBrand;
  }

  auto& slot = unboundBrands[schema];
  if (slot == nullptr) {
    slot = &arena.allocate<_::RawBrandedSchema>();
    memset(slot, 0, sizeof(*slot));
    slot->generic = schema;
    auto deps = makeBrandedDependencies(schema, nullptr);
    slot->dependencies = deps.begin();
    slot->dependencyCount = deps.size();
  }

  return slot;
}

kj::Array<Schema> SchemaLoader::Impl::getAllLoaded() const {
  size_t count = 0;
  for (auto& schema: schemas) {
    if (schema.second->lazyInitializer == nullptr) ++count;
  }

  kj::Array<Schema> result = kj::heapArray<Schema>(count);
  size_t i = 0;
  for (auto& schema: schemas) {
    if (schema.second->lazyInitializer == nullptr) {
      result[i++] = Schema(&schema.second->defaultBrand);
    }
  }
  return result;
}

void SchemaLoader::Impl::requireStructSize(uint64_t id, uint dataWordCount, uint pointerCount) {
  auto& slot = structSizeRequirements[id];
  slot.dataWordCount = kj::max(slot.dataWordCount, dataWordCount);
  slot.pointerCount = kj::max(slot.pointerCount, pointerCount);

  auto iter = schemas.find(id);
  if (iter != schemas.end()) {
    applyStructSizeRequirement(iter->second, dataWordCount, pointerCount);
  }
}

kj::ArrayPtr<word> SchemaLoader::Impl::makeUncheckedNode(schema::Node::Reader node) {
  size_t size = node.totalSize().wordCount + 1;
  kj::ArrayPtr<word> result = arena.allocateArray<word>(size);
  memset(result.begin(), 0, size * sizeof(word));
  copyToUnchecked(node, result);
  return result;
}

kj::ArrayPtr<word> SchemaLoader::Impl::makeUncheckedNodeEnforcingSizeRequirements(
    schema::Node::Reader node) {
  if (node.isStruct()) {
    auto iter = structSizeRequirements.find(node.getId());
    if (iter != structSizeRequirements.end()) {
      auto requirement = iter->second;
      auto structNode = node.getStruct();
      if (structNode.getDataWordCount() < requirement.dataWordCount ||
          structNode.getPointerCount() < requirement.pointerCount) {
        return rewriteStructNodeWithSizes(node, requirement.dataWordCount,
                                          requirement.pointerCount);
      }
    }
  }

  return makeUncheckedNode(node);
}

kj::ArrayPtr<word> SchemaLoader::Impl::rewriteStructNodeWithSizes(
    schema::Node::Reader node, uint dataWordCount, uint pointerCount) {
  MallocMessageBuilder builder;
  builder.setRoot(node);

  auto root = builder.getRoot<schema::Node>();
  auto newStruct = root.getStruct();
  newStruct.setDataWordCount(kj::max(newStruct.getDataWordCount(), dataWordCount));
  newStruct.setPointerCount(kj::max(newStruct.getPointerCount(), pointerCount));

  return makeUncheckedNode(root);
}

void SchemaLoader::Impl::applyStructSizeRequirement(
    _::RawSchema* raw, uint dataWordCount, uint pointerCount) {
  auto node = readMessageUnchecked<schema::Node>(raw->encodedNode);

  auto structNode = node.getStruct();
  if (structNode.getDataWordCount() < dataWordCount ||
      structNode.getPointerCount() < pointerCount) {
    // Sizes need to be increased.  Must rewrite.
    kj::ArrayPtr<word> words = rewriteStructNodeWithSizes(node, dataWordCount, pointerCount);

    // We don't need to re-validate the node because we know this change could not possibly have
    // invalidated it.  Just remake the unchecked message.
    raw->encodedNode = words.begin();
    raw->encodedSize = words.size();
  }
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
#if __GNUC__
    __atomic_store_n(&mutableSchema->lazyInitializer, nullptr, __ATOMIC_RELEASE);
    __atomic_store_n(&mutableSchema->defaultBrand.lazyInitializer, nullptr, __ATOMIC_RELEASE);
#elif _MSC_VER
    std::atomic_thread_fence(std::memory_order_release);
    *static_cast<_::RawSchema::Initializer const* volatile*>(
        &mutableSchema->lazyInitializer) = nullptr;
    *static_cast<_::RawBrandedSchema::Initializer const* volatile*>(
        &mutableSchema->defaultBrand.lazyInitializer) = nullptr;
#else
#error "Platform not supported"
#endif
  }
}

void SchemaLoader::BrandedInitializerImpl::init(const _::RawBrandedSchema* schema) const {
  schema->generic->ensureInitialized();

  auto lock = loader.impl.lockExclusive();

  if (schema->lazyInitializer == nullptr) {
    // Never mind, someone beat us to it.
    return;
  }

  // Get the mutable version.
  auto iter = lock->get()->brands.find(SchemaBindingsPair { schema->generic, schema->scopes });
  KJ_ASSERT(iter != lock->get()->brands.end());

  _::RawBrandedSchema* mutableSchema = iter->second;
  KJ_ASSERT(mutableSchema == schema);

  // Construct its dependency map.
  auto deps = lock->get()->makeBrandedDependencies(mutableSchema->generic,
      kj::arrayPtr(mutableSchema->scopes, mutableSchema->scopeCount));
  mutableSchema->dependencies = deps.begin();
  mutableSchema->dependencyCount = deps.size();

  // It's initialized now, so disable the initializer.
#if __GNUC__
  __atomic_store_n(&mutableSchema->lazyInitializer, nullptr, __ATOMIC_RELEASE);
#elif _MSC_VER
  std::atomic_thread_fence(std::memory_order_release);
  *static_cast<_::RawBrandedSchema::Initializer const* volatile*>(
      &mutableSchema->lazyInitializer) = nullptr;
#else
#error "Platform not supported"
#endif
}

// =======================================================================================

SchemaLoader::SchemaLoader(): impl(kj::heap<Impl>(*this)) {}
SchemaLoader::SchemaLoader(const LazyLoadCallback& callback)
    : impl(kj::heap<Impl>(*this, callback)) {}
SchemaLoader::~SchemaLoader() noexcept(false) {}

Schema SchemaLoader::get(uint64_t id, schema::Brand::Reader brand, Schema scope) const {
  KJ_IF_MAYBE(result, tryGet(id, brand, scope)) {
    return *result;
  } else {
    KJ_FAIL_REQUIRE("no schema node loaded for id", kj::hex(id));
  }
}

kj::Maybe<Schema> SchemaLoader::tryGet(
    uint64_t id, schema::Brand::Reader brand, Schema scope) const {
  auto getResult = impl.lockShared()->get()->tryGet(id);
  if (getResult.schema == nullptr || getResult.schema->lazyInitializer != nullptr) {
    // This schema couldn't be found or has yet to be lazily loaded. If we have a lazy loader
    // callback, invoke it now to try to get it to load this schema.
    KJ_IF_MAYBE(c, getResult.callback) {
      c->load(*this, id);
    }
    getResult = impl.lockShared()->get()->tryGet(id);
  }
  if (getResult.schema != nullptr && getResult.schema->lazyInitializer == nullptr) {
    if (brand.getScopes().size() > 0) {
      auto brandedSchema = impl.lockExclusive()->get()->makeBranded(
          getResult.schema, brand, kj::arrayPtr(scope.raw->scopes, scope.raw->scopeCount));
      brandedSchema->ensureInitialized();
      return Schema(brandedSchema);
    } else {
      return Schema(&getResult.schema->defaultBrand);
    }
  } else {
    return nullptr;
  }
}

Schema SchemaLoader::getUnbound(uint64_t id) const {
  auto schema = get(id);
  return Schema(impl.lockExclusive()->get()->getUnbound(schema.raw->generic));
}

Type SchemaLoader::getType(schema::Type::Reader proto, Schema scope) const {
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
      return get(structType.getTypeId(), structType.getBrand(), scope).asStruct();
    }

    case schema::Type::ENUM: {
      auto enumType = proto.getEnum();
      return get(enumType.getTypeId(), enumType.getBrand(), scope).asEnum();
    }

    case schema::Type::INTERFACE: {
      auto interfaceType = proto.getInterface();
      return get(interfaceType.getTypeId(), interfaceType.getBrand(), scope)
          .asInterface();
    }

    case schema::Type::LIST:
      return ListSchema::of(getType(proto.getList().getElementType(), scope));

    case schema::Type::ANY_POINTER: {
      auto anyPointer = proto.getAnyPointer();
      switch (anyPointer.which()) {
        case schema::Type::AnyPointer::UNCONSTRAINED:
          return schema::Type::ANY_POINTER;
        case schema::Type::AnyPointer::PARAMETER: {
          auto param = anyPointer.getParameter();
          return scope.getBrandBinding(param.getScopeId(), param.getParameterIndex());
        }
        case schema::Type::AnyPointer::IMPLICIT_METHOD_PARAMETER:
          // We don't support binding implicit method params here.
          return schema::Type::ANY_POINTER;
      }

      KJ_UNREACHABLE;
    }
  }

  KJ_UNREACHABLE;
}

Schema SchemaLoader::load(const schema::Node::Reader& reader) {
  return Schema(&impl.lockExclusive()->get()->load(reader, false)->defaultBrand);
}

Schema SchemaLoader::loadOnce(const schema::Node::Reader& reader) const {
  auto locked = impl.lockExclusive();
  auto getResult = locked->get()->tryGet(reader.getId());
  if (getResult.schema == nullptr || getResult.schema->lazyInitializer != nullptr) {
    // Doesn't exist yet, or the existing schema is a placeholder and therefore has not yet been
    // seen publicly.  Go ahead and load the incoming reader.
    return Schema(&locked->get()->load(reader, false)->defaultBrand);
  } else {
    return Schema(&getResult.schema->defaultBrand);
  }
}

kj::Array<Schema> SchemaLoader::getAllLoaded() const {
  return impl.lockShared()->get()->getAllLoaded();
}

void SchemaLoader::loadNative(const _::RawSchema* nativeSchema) {
  impl.lockExclusive()->get()->loadNative(nativeSchema);
}

}  // namespace capnp
