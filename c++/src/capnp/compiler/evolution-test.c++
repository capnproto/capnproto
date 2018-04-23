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

// This is a fuzz test which randomly generates a schema for a struct one change at a time.
// Each modification is known a priori to be compatible or incompatible.  The type is compiled
// before and after the change and both versions are loaded into a SchemaLoader with the
// expectation that this will succeed if they are compatible and fail if they are not.  If
// the types are expected to be compatible, the test also constructs an instance of the old
// type and reads it as the new type, and vice versa.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema-loader.h>
#include <capnp/message.h>
#include <capnp/pretty-print.h>
#include "compiler.h"
#include <kj/function.h>
#include <kj/debug.h>
#include <stdlib.h>
#include <time.h>
#include <kj/main.h>
#include <kj/io.h>
#include <kj/miniposix.h>

namespace capnp {
namespace compiler {
namespace {

static const kj::StringPtr RFC3092[] = {"foo", "bar", "baz", "qux"};

template <typename T, size_t size>
T& chooseFrom(T (&arr)[size]) {
  return arr[rand() % size];
}
template <typename T>
auto chooseFrom(T arr) -> decltype(arr[0]) {
  return arr[rand() % arr.size()];
}

static Declaration::Builder addNested(Declaration::Builder parent) {
  auto oldNestedOrphan = parent.disownNestedDecls();
  auto oldNested = oldNestedOrphan.get();
  auto newNested = parent.initNestedDecls(oldNested.size() + 1);

  uint index = rand() % (oldNested.size() + 1);

  for (uint i = 0; i < index; i++) {
    newNested.setWithCaveats(i, oldNested[i]);
  }
  for (uint i = index + 1; i < newNested.size(); i++) {
    newNested.setWithCaveats(i, oldNested[i - 1]);
  }

  return newNested[index];
}

struct TypeOption {
  kj::StringPtr name;
  kj::ConstFunction<void(Expression::Builder)> makeValue;
};

static const TypeOption TYPE_OPTIONS[] = {
  { "Int32",
    [](Expression::Builder builder) {
      builder.setPositiveInt(rand() % (1 << 24));
    }},
  { "Float64",
    [](Expression::Builder builder) {
      builder.setPositiveInt(rand());
    }},
  { "Int8",
    [](Expression::Builder builder) {
      builder.setPositiveInt(rand() % 128);
    }},
  { "UInt16",
    [](Expression::Builder builder) {
      builder.setPositiveInt(rand() % (1 << 16));
    }},
  { "Bool",
    [](Expression::Builder builder) {
      builder.initRelativeName().setValue("true");
    }},
  { "Text",
    [](Expression::Builder builder) {
      builder.setString(chooseFrom(RFC3092));
    }},
  { "StructType",
    [](Expression::Builder builder) {
      auto assignment = builder.initTuple(1)[0];
      assignment.initNamed().setValue("i");
      assignment.initValue().setPositiveInt(rand() % (1 << 24));
    }},
  { "EnumType",
    [](Expression::Builder builder) {
      builder.initRelativeName().setValue(chooseFrom(RFC3092));
    }},
};

void setDeclName(Expression::Builder decl, kj::StringPtr name) {
  decl.initRelativeName().setValue(name);
}

static kj::ConstFunction<void(Expression::Builder)> randomizeType(Expression::Builder type) {
  auto option = &chooseFrom(TYPE_OPTIONS);

  if (rand() % 4 == 0) {
    auto app = type.initApplication();
    setDeclName(app.initFunction(), "List");
    setDeclName(app.initParams(1)[0].initValue(), option->name);
    return [option](Expression::Builder builder) {
      for (auto element: builder.initList(rand() % 4 + 1)) {
        option->makeValue(element);
      }
    };
  } else {
    setDeclName(type, option->name);
    return option->makeValue.reference();
  }
}

enum ChangeKind {
  NO_CHANGE,
  COMPATIBLE,
  INCOMPATIBLE,

  SUBTLY_COMPATIBLE
  // The change is technically compatible on the wire, but SchemaLoader will complain.
};

struct ChangeInfo {
  ChangeKind kind;
  kj::String description;

  ChangeInfo(): kind(NO_CHANGE) {}
  ChangeInfo(ChangeKind kind, kj::StringPtr description)
      : kind(kind), description(kj::str(description)) {}
  ChangeInfo(ChangeKind kind, kj::String&& description)
      : kind(kind), description(kj::mv(description)) {}
};

extern kj::ArrayPtr<kj::ConstFunction<ChangeInfo(Declaration::Builder, uint&, bool)>> STRUCT_MODS;
extern kj::ArrayPtr<kj::ConstFunction<ChangeInfo(Declaration::Builder, uint&, bool)>> FIELD_MODS;

// ================================================================================

static ChangeInfo declChangeName(Declaration::Builder decl, uint& nextOrdinal,
                                 bool scopeHasUnion) {
  auto name = decl.getName();
  if (name.getValue().size() == 0) {
    // Naming an unnamed union.
    name.setValue(kj::str("unUnnamed", nextOrdinal));
    return { SUBTLY_COMPATIBLE, "Assign name to unnamed union." };
  } else {
    name.setValue(kj::str(name.getValue(), "Xx"));
    return { COMPATIBLE, "Rename declaration." };
  }
}

static ChangeInfo structAddField(Declaration::Builder decl, uint& nextOrdinal, bool scopeHasUnion) {
  auto fieldDecl = addNested(decl);

  uint ordinal = nextOrdinal++;

  fieldDecl.initName().setValue(kj::str("f", ordinal));
  fieldDecl.getId().initOrdinal().setValue(ordinal);

  auto field = fieldDecl.initField();

  auto makeValue = randomizeType(field.initType());
  if (rand() % 4 == 0) {
    makeValue(field.getDefaultValue().initValue());
  } else {
    field.getDefaultValue().setNone();
  }
  return { COMPATIBLE, "Add field." };
}

static ChangeInfo structModifyField(Declaration::Builder decl, uint& nextOrdinal,
                                    bool scopeHasUnion) {
  auto nested = decl.getNestedDecls();

  if (nested.size() == 0) {
    return { NO_CHANGE, "Modify field, but there were none to modify." };
  }

  auto field = chooseFrom(nested);

  bool hasUnion = false;
  if (decl.isUnion()) {
    hasUnion = true;
  } else {
    for (auto n: nested) {
      if (n.isUnion() && n.getName().getValue().size() == 0) {
        hasUnion = true;
        break;
      }
    }
  }

  if (field.isGroup() || field.isUnion()) {
    return chooseFrom(STRUCT_MODS)(field, nextOrdinal, hasUnion);
  } else {
    return chooseFrom(FIELD_MODS)(field, nextOrdinal, hasUnion);
  }
}

static ChangeInfo structGroupifyFields(
    Declaration::Builder decl, uint& nextOrdinal, bool scopeHasUnion) {
  // Place a random subset of the fields into a group.

  if (decl.isUnion()) {
    return { NO_CHANGE,
      "Randomly make a group out of some fields, but I can't do this to a union." };
  }

  kj::Vector<Orphan<Declaration>> groupified;
  kj::Vector<Orphan<Declaration>> notGroupified;
  auto orphanage = Orphanage::getForMessageContaining(decl);

  for (auto nested: decl.getNestedDecls()) {
    if (rand() % 2) {
      groupified.add(orphanage.newOrphanCopy(nested.asReader()));
    } else {
      notGroupified.add(orphanage.newOrphanCopy(nested.asReader()));
    }
  }

  if (groupified.size() == 0) {
    return { NO_CHANGE,
      "Randomly make a group out of some fields, but I ended up choosing none of them." };
  }

  auto newNested = decl.initNestedDecls(notGroupified.size() + 1);
  uint index = rand() % (notGroupified.size() + 1);

  for (uint i = 0; i < index; i++) {
    newNested.adoptWithCaveats(i, kj::mv(notGroupified[i]));
  }
  for (uint i = index; i < notGroupified.size(); i++) {
    newNested.adoptWithCaveats(i + 1, kj::mv(notGroupified[i]));
  }

  auto newGroup = newNested[index];
  auto groupNested = newGroup.initNestedDecls(groupified.size());
  for (uint i = 0; i < groupified.size(); i++) {
    groupNested.adoptWithCaveats(i, kj::mv(groupified[i]));
  }

  newGroup.initName().setValue(kj::str("g", nextOrdinal, "x", groupNested[0].getName().getValue()));
  newGroup.getId().setUnspecified();
  newGroup.setGroup();

  return { SUBTLY_COMPATIBLE, "Randomly group some set of existing fields." };
}

static ChangeInfo structPermuteFields(
    Declaration::Builder decl, uint& nextOrdinal, bool scopeHasUnion) {
  if (decl.getNestedDecls().size() == 0) {
    return { NO_CHANGE, "Permute field code order, but there were none." };
  }

  auto oldOrphan = decl.disownNestedDecls();
  auto old = oldOrphan.get();

  KJ_STACK_ARRAY(uint, mapping, old.size(), 16, 64);

  for (uint i = 0; i < mapping.size(); i++) {
    mapping[i] = i;
  }
  for (uint i = mapping.size() - 1; i > 0; i--) {
    uint j = rand() % i;
    uint temp = mapping[j];
    mapping[j] = mapping[i];
    mapping[i] = temp;
  }

  auto newNested = decl.initNestedDecls(old.size());
  for (uint i = 0; i < old.size(); i++) {
    newNested.setWithCaveats(i, old[mapping[i]]);
  }

  return { COMPATIBLE, "Permute field code order." };
}

kj::ConstFunction<ChangeInfo(Declaration::Builder, uint&, bool)> STRUCT_MODS_[] = {
  structAddField,
  structAddField,
  structAddField,
  structModifyField,
  structModifyField,
  structModifyField,
  structPermuteFields,
  declChangeName,
  structGroupifyFields     // do more rarely because it creates slowness
};
kj::ArrayPtr<kj::ConstFunction<ChangeInfo(Declaration::Builder, uint&, bool)>>
    STRUCT_MODS = STRUCT_MODS_;

// ================================================================================

static ChangeInfo fieldUpgradeList(Declaration::Builder decl, uint& nextOrdinal,
                                   bool scopeHasUnion) {
  // Upgrades a non-struct list to a struct list.

  auto field = decl.getField();
  if (field.getDefaultValue().isValue()) {
    return { NO_CHANGE, "Upgrade primitive list to struct list, but it had a default value." };
  }

  auto type = field.getType();
  if (!type.isApplication()) {
    return { NO_CHANGE, "Upgrade primitive list to struct list, but it wasn't a list." };
  }
  auto typeParams = type.getApplication().getParams();

  auto elementType = typeParams[0].getValue();
  auto relativeName = elementType.getRelativeName();
  auto nameText = relativeName.asReader().getValue();
  if (nameText == "StructType" || nameText.endsWith("Struct")) {
    return { NO_CHANGE, "Upgrade primitive list to struct list, but it was already a struct list."};
  }
  if (nameText == "Bool") {
    return { NO_CHANGE, "Upgrade primitive list to struct list, but bool lists can't be upgraded."};
  }

  relativeName.setValue(kj::str(nameText, "Struct"));
  return { COMPATIBLE, "Upgrade primitive list to struct list" };
}

static ChangeInfo fieldExpandGroup(Declaration::Builder decl, uint& nextOrdinal,
                                   bool scopeHasUnion) {
  Declaration::Builder newDecl = decl.initNestedDecls(1)[0];
  newDecl.adoptName(decl.disownName());
  newDecl.getId().adoptOrdinal(decl.getId().disownOrdinal());

  auto field = decl.getField();
  auto newField = newDecl.initField();

  newField.adoptType(field.disownType());
  if (field.getDefaultValue().isValue()) {
    newField.getDefaultValue().adoptValue(field.getDefaultValue().disownValue());
  } else {
    newField.getDefaultValue().setNone();
  }

  decl.initName().setValue(kj::str("g", newDecl.getName().getValue()));
  decl.getId().setUnspecified();

  if (rand() % 2 == 0) {
    decl.setGroup();
  } else {
    decl.setUnion();
    if (!scopeHasUnion && rand() % 2 == 0) {
      // Make it an unnamed union.
      decl.getName().setValue("");
    }
    structAddField(decl, nextOrdinal, scopeHasUnion);  // union must have two members
  }

  return { COMPATIBLE, "Wrap a field in a singleton group." };
}

static ChangeInfo fieldChangeType(Declaration::Builder decl, uint& nextOrdinal,
                                  bool scopeHasUnion) {
  auto field = decl.getField();

  if (field.getDefaultValue().isNone()) {
    // Change the type.
    auto type = field.getType();
    while (type.isApplication()) {
      // Either change the list parameter, or revert to a non-list.
      if (rand() % 2) {
        type = type.getApplication().getParams()[0].getValue();
      } else {
        type.initRelativeName();
      }
    }
    auto typeName = type.getRelativeName();
    if (typeName.asReader().getValue().startsWith("Text")) {
      typeName.setValue("Int32");
    } else {
      typeName.setValue("Text");
    }
    return { INCOMPATIBLE, "Change the type of a field." };
  } else {
    // Change the default value.
    auto dval = field.getDefaultValue().getValue();
    switch (dval.which()) {
      case Expression::UNKNOWN: KJ_FAIL_ASSERT("unknown value expression?");
      case Expression::POSITIVE_INT: dval.setPositiveInt(dval.getPositiveInt() ^ 1); break;
      case Expression::NEGATIVE_INT: dval.setNegativeInt(dval.getNegativeInt() ^ 1); break;
      case Expression::FLOAT: dval.setFloat(-dval.getFloat()); break;
      case Expression::RELATIVE_NAME: {
        auto name = dval.getRelativeName();
        auto nameText = name.asReader().getValue();
        if (nameText == "true") {
          name.setValue("false");
        } else if (nameText == "false") {
          name.setValue("true");
        } else if (nameText == "foo") {
          name.setValue("bar");
        } else {
          name.setValue("foo");
        }
        break;
      }
      case Expression::STRING:
      case Expression::BINARY:
      case Expression::LIST:
      case Expression::TUPLE:
        return { NO_CHANGE, "Change the default value of a field, but it's a pointer field." };

      case Expression::ABSOLUTE_NAME:
      case Expression::IMPORT:
      case Expression::EMBED:
      case Expression::APPLICATION:
      case Expression::MEMBER:
        KJ_FAIL_ASSERT("Unexpected expression type.");
    }
    return { INCOMPATIBLE, "Change the default value of a pritimive field." };
  }
}

kj::ConstFunction<ChangeInfo(Declaration::Builder, uint&, bool)> FIELD_MODS_[] = {
  fieldUpgradeList,
  fieldExpandGroup,
  fieldChangeType,
  declChangeName
};
kj::ArrayPtr<kj::ConstFunction<ChangeInfo(Declaration::Builder, uint&, bool)>>
    FIELD_MODS = FIELD_MODS_;

// ================================================================================

uint getOrdinal(StructSchema::Field field) {
  auto proto = field.getProto();
  if (proto.getOrdinal().isExplicit()) {
    return proto.getOrdinal().getExplicit();
  }

  KJ_ASSERT(proto.isGroup());

  auto group = field.getType().asStruct();
  return getOrdinal(group.getFields()[0]);
}

Orphan<DynamicStruct> makeExampleStruct(
    Orphanage orphanage, StructSchema schema, uint sharedOrdinalCount);
void checkExampleStruct(DynamicStruct::Reader reader, uint sharedOrdinalCount);

Orphan<DynamicValue> makeExampleValue(
    Orphanage orphanage, uint ordinal, Type type, uint sharedOrdinalCount) {
  switch (type.which()) {
    case schema::Type::INT32: return ordinal * 47327;
    case schema::Type::FLOAT64: return ordinal * 313.25;
    case schema::Type::INT8: return int(ordinal % 256) - 128;
    case schema::Type::UINT16: return ordinal * 13;
    case schema::Type::BOOL: return ordinal % 2 == 0;
    case schema::Type::TEXT: return orphanage.newOrphanCopy(Text::Reader(kj::str(ordinal)));
    case schema::Type::STRUCT: {
      auto structType = type.asStruct();
      auto result = orphanage.newOrphan(structType);
      auto builder = result.get();

      KJ_IF_MAYBE(fieldI, structType.findFieldByName("i")) {
        // Type is "StructType"
        builder.set(*fieldI, ordinal);
      } else {
        // Type is "Int32Struct" or the like.
        auto field = structType.getFieldByName("f0");
        builder.adopt(field, makeExampleValue(
            orphanage, ordinal, field.getType(), sharedOrdinalCount));
      }

      return kj::mv(result);
    }
    case schema::Type::ENUM: {
      auto enumerants = type.asEnum().getEnumerants();
      return DynamicEnum(enumerants[ordinal %enumerants.size()]);
    }
    case schema::Type::LIST: {
      auto listType = type.asList();
      auto elementType = listType.getElementType();
      auto result = orphanage.newOrphan(listType, 1);
      result.get().adopt(0, makeExampleValue(
          orphanage, ordinal, elementType, sharedOrdinalCount));
      return kj::mv(result);
    }
    default:
      KJ_FAIL_ASSERT("You added a new possible field type!");
  }
}

void checkExampleValue(DynamicValue::Reader value, uint ordinal, schema::Type::Reader type,
                       uint sharedOrdinalCount) {
  switch (type.which()) {
    case schema::Type::INT32: KJ_ASSERT(value.as<int32_t>() == ordinal * 47327); break;
    case schema::Type::FLOAT64: KJ_ASSERT(value.as<double>() == ordinal * 313.25); break;
    case schema::Type::INT8: KJ_ASSERT(value.as<int8_t>() == int(ordinal % 256) - 128); break;
    case schema::Type::UINT16: KJ_ASSERT(value.as<uint16_t>() == ordinal * 13); break;
    case schema::Type::BOOL: KJ_ASSERT(value.as<bool>() == (ordinal % 2 == 0)); break;
    case schema::Type::TEXT: KJ_ASSERT(value.as<Text>() == kj::str(ordinal)); break;
    case schema::Type::STRUCT: {
      auto structValue = value.as<DynamicStruct>();
      auto structType = structValue.getSchema();

      KJ_IF_MAYBE(fieldI, structType.findFieldByName("i")) {
        // Type is "StructType"
        KJ_ASSERT(structValue.get(*fieldI).as<uint32_t>() == ordinal);
      } else {
        // Type is "Int32Struct" or the like.
        auto field = structType.getFieldByName("f0");
        checkExampleValue(structValue.get(field), ordinal,
                          field.getProto().getSlot().getType(), sharedOrdinalCount);
      }
      break;
    }
    case schema::Type::ENUM: {
      auto enumerant = KJ_ASSERT_NONNULL(value.as<DynamicEnum>().getEnumerant());
      KJ_ASSERT(enumerant.getIndex() ==
          ordinal % enumerant.getContainingEnum().getEnumerants().size());
      break;
    }
    case schema::Type::LIST:
      checkExampleValue(value.as<DynamicList>()[0], ordinal, type.getList().getElementType(),
                        sharedOrdinalCount);
      break;
    default:
      KJ_FAIL_ASSERT("You added a new possible field type!");
  }
}

void setExampleField(DynamicStruct::Builder builder, StructSchema::Field field,
                     uint sharedOrdinalCount) {
  auto fieldProto = field.getProto();
  switch (fieldProto.which()) {
    case schema::Field::SLOT:
      builder.adopt(field, makeExampleValue(
          Orphanage::getForMessageContaining(builder),
          getOrdinal(field), field.getType(), sharedOrdinalCount));
      break;
    case schema::Field::GROUP:
      builder.adopt(field, makeExampleStruct(
          Orphanage::getForMessageContaining(builder),
          field.getType().asStruct(), sharedOrdinalCount));
      break;
  }
}

void checkExampleField(DynamicStruct::Reader reader, StructSchema::Field field,
                       uint sharedOrdinalCount) {
  auto fieldProto = field.getProto();
  switch (fieldProto.which()) {
    case schema::Field::SLOT: {
      uint ordinal = getOrdinal(field);
      if (ordinal < sharedOrdinalCount) {
        checkExampleValue(reader.get(field), ordinal,
                          fieldProto.getSlot().getType(), sharedOrdinalCount);
      }
      break;
    }
    case schema::Field::GROUP:
      checkExampleStruct(reader.get(field).as<DynamicStruct>(), sharedOrdinalCount);
      break;
  }
}

Orphan<DynamicStruct> makeExampleStruct(
    Orphanage orphanage, StructSchema schema, uint sharedOrdinalCount) {
  // Initialize all fields of the struct via reflection, such that they can be verified using
  // a different version of the struct.  sharedOrdinalCount is the number of ordinals shared by
  // the two versions.  This is used mainly to avoid setting union members that the other version
  // doesn't have.

  Orphan<DynamicStruct> result = orphanage.newOrphan(schema);
  auto builder = result.get();

  for (auto field: schema.getNonUnionFields()) {
    setExampleField(builder, field, sharedOrdinalCount);
  }

  auto unionFields = schema.getUnionFields();

  // Pretend the union doesn't have any fields that aren't in the shared ordinal range.
  uint range = unionFields.size();
  while (range > 0 && getOrdinal(unionFields[range - 1]) >= sharedOrdinalCount) {
    --range;
  }

  if (range > 0) {
    auto field = unionFields[getOrdinal(unionFields[0]) % range];
    setExampleField(builder, field, sharedOrdinalCount);
  }

  return kj::mv(result);
}

void checkExampleStruct(DynamicStruct::Reader reader, uint sharedOrdinalCount) {
  auto schema = reader.getSchema();

  for (auto field: schema.getNonUnionFields()) {
    checkExampleField(reader, field, sharedOrdinalCount);
  }

  auto unionFields = schema.getUnionFields();

  // Pretend the union doesn't have any fields that aren't in the shared ordinal range.
  uint range = unionFields.size();
  while (range > 0 && getOrdinal(unionFields[range - 1]) >= sharedOrdinalCount) {
    --range;
  }

  if (range > 0) {
    auto field = unionFields[getOrdinal(unionFields[0]) % range];
    checkExampleField(reader, field, sharedOrdinalCount);
  }
}

// ================================================================================

class ModuleImpl final: public Module {
public:
  explicit ModuleImpl(ParsedFile::Reader content): content(content) {}

  kj::StringPtr getSourceName() override { return "evolving-schema.capnp"; }
  Orphan<ParsedFile> loadContent(Orphanage orphanage) override {
    return orphanage.newOrphanCopy(content);
  }
  kj::Maybe<Module&> importRelative(kj::StringPtr importPath) override {
    return nullptr;
  }
  kj::Maybe<kj::Array<const byte>> embedRelative(kj::StringPtr embedPath) override {
    return nullptr;
  }

  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    KJ_FAIL_ASSERT("Unexpected parse error.", startByte, endByte, message);
  }
  bool hadErrors() override {
    return false;
  }

private:
  ParsedFile::Reader content;
};

static void loadStructAndGroups(const SchemaLoader& src, SchemaLoader& dst, uint64_t id) {
  auto proto = src.get(id).getProto();
  dst.load(proto);

  for (auto field: proto.getStruct().getFields()) {
    if (field.isGroup()) {
      loadStructAndGroups(src, dst, field.getGroup().getTypeId());
    }
  }
}

static kj::Maybe<kj::Exception> loadFile(
    ParsedFile::Reader file, SchemaLoader& loader, bool allNodes,
    kj::Maybe<kj::Own<MallocMessageBuilder>>& messageBuilder,
    uint sharedOrdinalCount) {
  Compiler compiler;
  ModuleImpl module(file);
  KJ_ASSERT(compiler.add(module) == 0x8123456789abcdefllu);

  if (allNodes) {
    // Eagerly compile and load the whole thing.
    compiler.eagerlyCompile(0x8123456789abcdefllu, Compiler::ALL_RELATED_NODES);

    KJ_IF_MAYBE(m, messageBuilder) {
      // Build an example struct using the compiled schema.
      m->get()->adoptRoot(makeExampleStruct(
          m->get()->getOrphanage(), compiler.getLoader().get(0x823456789abcdef1llu).asStruct(),
          sharedOrdinalCount));
    }

    for (auto schema: compiler.getLoader().getAllLoaded()) {
      loader.load(schema.getProto());
    }
    return nullptr;
  } else {
    // Compile the file root so that the children are findable, then load the specific child
    // we want.
    compiler.eagerlyCompile(0x8123456789abcdefllu, Compiler::NODE);

    KJ_IF_MAYBE(m, messageBuilder) {
      // Check that the example struct matches the compiled schema.
      auto root = m->get()->getRoot<DynamicStruct>(
          compiler.getLoader().get(0x823456789abcdef1llu).asStruct()).asReader();
      KJ_CONTEXT(root);
      checkExampleStruct(root, sharedOrdinalCount);
    }

    return kj::runCatchingExceptions([&]() {
      loadStructAndGroups(compiler.getLoader(), loader, 0x823456789abcdef1llu);
    });
  }
}

bool checkChange(ParsedFile::Reader file1, ParsedFile::Reader file2, ChangeKind changeKind,
                 uint sharedOrdinalCount) {
  // Try loading file1 followed by file2 into the same SchemaLoader, expecting it to behave
  // according to changeKind.  Returns true if the files are both expected to be compatible and
  // actually are -- the main loop uses this to decide which version to keep

  kj::Maybe<kj::Own<MallocMessageBuilder>> exampleBuilder;

  if (changeKind != INCOMPATIBLE) {
    // For COMPATIBLE and SUBTLY_COMPATIBLE changes, build an example message with one schema
    // and check it with the other.
    exampleBuilder = kj::heap<MallocMessageBuilder>();
  }

  SchemaLoader loader;
  loadFile(file1, loader, true, exampleBuilder, sharedOrdinalCount);
  auto exception = loadFile(file2, loader, false, exampleBuilder, sharedOrdinalCount);

  if (changeKind == COMPATIBLE) {
    KJ_IF_MAYBE(e, exception) {
      kj::getExceptionCallback().onFatalException(kj::mv(*e));
      return false;
    } else {
      return true;
    }
  } else if (changeKind == INCOMPATIBLE) {
    KJ_ASSERT(exception != nullptr, file1, file2);
    return false;
  } else {
    KJ_ASSERT(changeKind == SUBTLY_COMPATIBLE);

    // SchemaLoader is allowed to throw an exception in this case, but we ignore it.
    return true;
  }
}

void doTest() {
  auto builder = kj::heap<MallocMessageBuilder>();

  {
    // Set up the basic file decl.
    auto parsedFile = builder->initRoot<ParsedFile>();
    auto file = parsedFile.initRoot();
    file.setFile();
    file.initId().initUid().setValue(0x8123456789abcdefllu);
    auto decls = file.initNestedDecls(3 + kj::size(TYPE_OPTIONS));

    {
      auto decl = decls[0];
      decl.initName().setValue("EvolvingStruct");
      decl.initId().initUid().setValue(0x823456789abcdef1llu);
      decl.setStruct();
    }
    {
      auto decl = decls[1];
      decl.initName().setValue("StructType");
      decl.setStruct();

      auto fieldDecl = decl.initNestedDecls(1)[0];
      fieldDecl.initName().setValue("i");
      fieldDecl.getId().initOrdinal().setValue(0);
      auto field = fieldDecl.initField();
      setDeclName(field.initType(), "UInt32");
    }
    {
      auto decl = decls[2];
      decl.initName().setValue("EnumType");
      decl.setEnum();

      auto enumerants = decl.initNestedDecls(4);

      for (uint i = 0; i < kj::size(RFC3092); i++) {
        auto enumerantDecl = enumerants[i];
        enumerantDecl.initName().setValue(RFC3092[i]);
        enumerantDecl.getId().initOrdinal().setValue(i);
        enumerantDecl.setEnumerant();
      }
    }

    // For each of TYPE_OPTIONS, declare a struct type that contains that type as its @0 field.
    for (uint i = 0; i < kj::size(TYPE_OPTIONS); i++) {
      auto decl = decls[3 + i];
      auto& option = TYPE_OPTIONS[i];

      decl.initName().setValue(kj::str(option.name, "Struct"));
      decl.setStruct();

      auto fieldDecl = decl.initNestedDecls(1)[0];
      fieldDecl.initName().setValue("f0");
      fieldDecl.getId().initOrdinal().setValue(0);
      auto field = fieldDecl.initField();
      setDeclName(field.initType(), option.name);

      uint ordinal = 1;
      for (auto j: kj::range(0, rand() % 4)) {
        (void)j;
        structAddField(decl, ordinal, false);
      }
    }
  }

  uint nextOrdinal = 0;

  for (uint i = 0; i < 96; i++) {
    uint oldOrdinalCount = nextOrdinal;

    auto newBuilder = kj::heap<MallocMessageBuilder>();
    newBuilder->setRoot(builder->getRoot<ParsedFile>().asReader());

    auto parsedFile = newBuilder->getRoot<ParsedFile>();
    Declaration::Builder decl = parsedFile.getRoot().getNestedDecls()[0];

    // Apply a random modification.
    ChangeInfo changeInfo;
    while (changeInfo.kind == NO_CHANGE) {
      auto& mod = chooseFrom(STRUCT_MODS);
      changeInfo = mod(decl, nextOrdinal, false);
    }

    KJ_CONTEXT(changeInfo.description);

    if (checkChange(builder->getRoot<ParsedFile>(), parsedFile, changeInfo.kind, oldOrdinalCount) &&
        checkChange(parsedFile, builder->getRoot<ParsedFile>(), changeInfo.kind, oldOrdinalCount)) {
      builder = kj::mv(newBuilder);
    }
  }
}

class EvolutionTestMain {
public:
  explicit EvolutionTestMain(kj::ProcessContext& context)
      : context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "(unknown version)",
        "Integration test / fuzzer which randomly modifies schemas is backwards-compatible ways "
        "and verifies that they do actually remain compatible.")
        .addOptionWithArg({"seed"}, KJ_BIND_METHOD(*this, setSeed), "<num>",
            "Set random number seed to <num>.  By default, time() is used.")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

  kj::MainBuilder::Validity setSeed(kj::StringPtr value) {
    char* end;
    seed = strtol(value.cStr(), &end, 0);
    if (value.size() == 0 || *end != '\0') {
      return "not an integer";
    } else {
      return true;
    }
  }

  kj::MainBuilder::Validity run() {
    // https://github.com/sandstorm-io/capnproto/issues/344 describes an obscure bug in the layout
    // algorithm, the fix for which breaks backwards-compatibility for any schema triggering the
    // bug. In order to avoid silently breaking protocols, we are temporarily throwing an exception
    // in cases where this bug would have occurred, so that people can decide what to do.
    // However, the evolution test can occasionally trigger the bug (depending on the random path
    // it takes). Rather than try to avoid it, we disable the exception-throwing, because the bug
    // is actually fixed, and the exception is only there to raise awareness of the compatibility
    // concerns.
    //
    // On Linux, seed 1467142714 (for example) will trigger the exception (without this env var).
#if defined(__MINGW32__) || defined(_MSC_VER)
    putenv("CAPNP_IGNORE_ISSUE_344=1");
#else
    setenv("CAPNP_IGNORE_ISSUE_344", "1", true);
#endif

    srand(seed);

    {
      kj::String text = kj::str(
          "Randomly testing backwards-compatibility scenarios with seed: ", seed, "\n");
      kj::FdOutputStream(STDOUT_FILENO).write(text.begin(), text.size());
    }

    KJ_CONTEXT(seed, "PLEASE REPORT THIS FAILURE AND INCLUDE THE SEED");

    doTest();

    return true;
  }

private:
  kj::ProcessContext& context;
  uint seed = time(nullptr);
};

}  // namespace
}  // namespace compiler
}  // namespace capnp

KJ_MAIN(capnp::compiler::EvolutionTestMain);
