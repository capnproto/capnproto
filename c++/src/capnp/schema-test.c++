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

#define CAPNP_TESTING_CAPNP 1

#include "schema.h"
#include <kj/compat/gtest.h>
#include "test-util.h"

// TODO(cleanup): Auto-generate stringification functions for union discriminants.
namespace capnp {
namespace schema {
inline kj::String KJ_STRINGIFY(Type::Which which) {
  return kj::str(static_cast<uint16_t>(which));
}
}  // namespace schema
}  // namespace capnp

namespace capnp {
namespace _ {  // private
namespace {

TEST(Schema, Structs) {
  StructSchema schema = Schema::from<TestAllTypes>();

  EXPECT_EQ(typeId<TestAllTypes>(), schema.getProto().getId());

  EXPECT_TRUE(schema.getDependency(typeId<TestEnum>()) == Schema::from<TestEnum>());
  EXPECT_TRUE(schema.getDependency(typeId<TestEnum>()) != schema);
  EXPECT_TRUE(schema.getDependency(typeId<TestAllTypes>()) == Schema::from<TestAllTypes>());
  EXPECT_TRUE(schema.getDependency(typeId<TestAllTypes>()) == schema);
  EXPECT_NONFATAL_FAILURE(schema.getDependency(typeId<TestDefaults>()));

  EXPECT_TRUE(schema.asStruct() == schema);
  EXPECT_NONFATAL_FAILURE(schema.asEnum());
  EXPECT_NONFATAL_FAILURE(schema.asInterface());

  ASSERT_EQ(schema.getFields().size(), schema.getProto().getStruct().getFields().size());
  StructSchema::Field field = schema.getFields()[0];
  EXPECT_EQ("voidField", field.getProto().getName());
  EXPECT_TRUE(field.getContainingStruct() == schema);

  StructSchema::Field lookup = schema.getFieldByName("voidField");
  EXPECT_TRUE(lookup == field);
  EXPECT_TRUE(lookup != schema.getFields()[1]);
  EXPECT_FALSE(lookup.getProto().getSlot().getHadExplicitDefault());

  EXPECT_FALSE(schema.getFieldByName("int32Field").getProto().getSlot().getHadExplicitDefault());

  EXPECT_TRUE(Schema::from<TestDefaults>().getFieldByName("int32Field")
      .getProto().getSlot().getHadExplicitDefault());

  EXPECT_TRUE(schema.findFieldByName("noSuchField") == nullptr);

  EXPECT_TRUE(schema.findFieldByName("int32Field") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("float32List") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("dataList") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("textField") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("structField") != nullptr);
}

TEST(Schema, FieldLookupOutOfOrder) {
  // Tests that name lookup works correctly when the fields are defined out-of-order in the schema
  // file.
  auto schema = Schema::from<test::TestOutOfOrder>().asStruct();

  EXPECT_EQ("qux", schema.getFields()[0].getProto().getName());
  EXPECT_EQ("grault", schema.getFields()[1].getProto().getName());
  EXPECT_EQ("bar", schema.getFields()[2].getProto().getName());
  EXPECT_EQ("foo", schema.getFields()[3].getProto().getName());
  EXPECT_EQ("corge", schema.getFields()[4].getProto().getName());
  EXPECT_EQ("waldo", schema.getFields()[5].getProto().getName());
  EXPECT_EQ("quux", schema.getFields()[6].getProto().getName());
  EXPECT_EQ("garply", schema.getFields()[7].getProto().getName());
  EXPECT_EQ("baz", schema.getFields()[8].getProto().getName());

  EXPECT_EQ(3, schema.getFieldByName("foo").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(2, schema.getFieldByName("bar").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(8, schema.getFieldByName("baz").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(0, schema.getFieldByName("qux").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(6, schema.getFieldByName("quux").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(4, schema.getFieldByName("corge").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(1, schema.getFieldByName("grault").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(7, schema.getFieldByName("garply").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(5, schema.getFieldByName("waldo").getProto().getOrdinal().getExplicit());
}

TEST(Schema, Unions) {
  auto schema = Schema::from<TestUnion>().asStruct();

  EXPECT_TRUE(schema.findFieldByName("bit0") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("u1f0s8") == nullptr);

  auto union1 = schema.getFieldByName("union1");
  auto union1g = schema.getDependency(union1.getProto().getGroup().getTypeId()).asStruct();
  EXPECT_EQ(schema, union1g.getDependency(union1g.getProto().getScopeId()));
  EXPECT_TRUE(union1g.findFieldByName("bin0") == nullptr);

  auto u1f0s8 = union1g.getFieldByName("u1f0s8");
  EXPECT_EQ("u1f0s8", u1f0s8.getProto().getName());
  EXPECT_TRUE(u1f0s8.getContainingStruct() == union1g);

  EXPECT_TRUE(union1g.findFieldByName("u1f1s8") != nullptr);
  EXPECT_TRUE(union1g.findFieldByName("u1f0s32") != nullptr);
  EXPECT_TRUE(union1g.findFieldByName("u1f0sp") != nullptr);
  EXPECT_TRUE(union1g.findFieldByName("u1f1s1") != nullptr);

  EXPECT_TRUE(union1g.findFieldByName("u0f0s1") == nullptr);
  EXPECT_TRUE(union1g.findFieldByName("u2f0s8") == nullptr);
  EXPECT_TRUE(union1g.findFieldByName("noSuchField") == nullptr);
}

TEST(Schema, Enums) {
  EnumSchema schema = Schema::from<TestEnum>();

  EXPECT_EQ(typeId<TestEnum>(), schema.getProto().getId());

  EXPECT_NONFATAL_FAILURE(schema.getDependency(typeId<TestAllTypes>()));
  EXPECT_NONFATAL_FAILURE(schema.getDependency(typeId<TestEnum>()));

  EXPECT_NONFATAL_FAILURE(schema.asStruct());
  EXPECT_NONFATAL_FAILURE(schema.asInterface());
  EXPECT_TRUE(schema.asEnum() == schema);

  ASSERT_EQ(schema.getEnumerants().size(),
            schema.getProto().getEnum().getEnumerants().size());
  EnumSchema::Enumerant enumerant = schema.getEnumerants()[0];
  EXPECT_EQ("foo", enumerant.getProto().getName());
  EXPECT_TRUE(enumerant.getContainingEnum() == schema);

  EnumSchema::Enumerant lookup = schema.getEnumerantByName("foo");
  EXPECT_TRUE(lookup == enumerant);
  EXPECT_TRUE(lookup != schema.getEnumerants()[1]);

  EXPECT_TRUE(schema.findEnumerantByName("noSuchEnumerant") == nullptr);

  EXPECT_TRUE(schema.findEnumerantByName("bar") != nullptr);
  EXPECT_TRUE(schema.findEnumerantByName("qux") != nullptr);
  EXPECT_TRUE(schema.findEnumerantByName("corge") != nullptr);
  EXPECT_TRUE(schema.findEnumerantByName("grault") != nullptr);
}

// TODO(someday):  Test interface schemas when interfaces are implemented.

TEST(Schema, Lists) {
  EXPECT_EQ(schema::Type::VOID, Schema::from<List<Void>>().whichElementType());
  EXPECT_EQ(schema::Type::BOOL, Schema::from<List<bool>>().whichElementType());
  EXPECT_EQ(schema::Type::INT8, Schema::from<List<int8_t>>().whichElementType());
  EXPECT_EQ(schema::Type::INT16, Schema::from<List<int16_t>>().whichElementType());
  EXPECT_EQ(schema::Type::INT32, Schema::from<List<int32_t>>().whichElementType());
  EXPECT_EQ(schema::Type::INT64, Schema::from<List<int64_t>>().whichElementType());
  EXPECT_EQ(schema::Type::UINT8, Schema::from<List<uint8_t>>().whichElementType());
  EXPECT_EQ(schema::Type::UINT16, Schema::from<List<uint16_t>>().whichElementType());
  EXPECT_EQ(schema::Type::UINT32, Schema::from<List<uint32_t>>().whichElementType());
  EXPECT_EQ(schema::Type::UINT64, Schema::from<List<uint64_t>>().whichElementType());
  EXPECT_EQ(schema::Type::FLOAT32, Schema::from<List<float>>().whichElementType());
  EXPECT_EQ(schema::Type::FLOAT64, Schema::from<List<double>>().whichElementType());
  EXPECT_EQ(schema::Type::TEXT, Schema::from<List<Text>>().whichElementType());
  EXPECT_EQ(schema::Type::DATA, Schema::from<List<Data>>().whichElementType());

  EXPECT_NONFATAL_FAILURE(Schema::from<List<uint16_t>>().getStructElementType());
  EXPECT_NONFATAL_FAILURE(Schema::from<List<uint16_t>>().getEnumElementType());
  EXPECT_NONFATAL_FAILURE(Schema::from<List<uint16_t>>().getInterfaceElementType());
  EXPECT_NONFATAL_FAILURE(Schema::from<List<uint16_t>>().getListElementType());

  {
    ListSchema schema = Schema::from<List<TestAllTypes>>();
    EXPECT_EQ(schema::Type::STRUCT, schema.whichElementType());
    EXPECT_TRUE(schema.getStructElementType() == Schema::from<TestAllTypes>());
    EXPECT_NONFATAL_FAILURE(schema.getEnumElementType());
    EXPECT_NONFATAL_FAILURE(schema.getInterfaceElementType());
    EXPECT_NONFATAL_FAILURE(schema.getListElementType());
  }

  {
    ListSchema schema = Schema::from<List<TestEnum>>();
    EXPECT_EQ(schema::Type::ENUM, schema.whichElementType());
    EXPECT_TRUE(schema.getEnumElementType() == Schema::from<TestEnum>());
    EXPECT_NONFATAL_FAILURE(schema.getStructElementType());
    EXPECT_NONFATAL_FAILURE(schema.getInterfaceElementType());
    EXPECT_NONFATAL_FAILURE(schema.getListElementType());
  }

  // TODO(someday):  Test interfaces.

  {
    ListSchema schema = Schema::from<List<List<int32_t>>>();
    EXPECT_EQ(schema::Type::LIST, schema.whichElementType());
    EXPECT_NONFATAL_FAILURE(schema.getStructElementType());
    EXPECT_NONFATAL_FAILURE(schema.getEnumElementType());
    EXPECT_NONFATAL_FAILURE(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::INT32, inner.whichElementType());
  }

  {
    ListSchema schema = Schema::from<List<List<TestAllTypes>>>();
    EXPECT_EQ(schema::Type::LIST, schema.whichElementType());
    EXPECT_NONFATAL_FAILURE(schema.getStructElementType());
    EXPECT_NONFATAL_FAILURE(schema.getEnumElementType());
    EXPECT_NONFATAL_FAILURE(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::STRUCT, inner.whichElementType());
    EXPECT_TRUE(inner.getStructElementType() == Schema::from<TestAllTypes>());
  }

  {
    ListSchema schema = Schema::from<List<List<TestEnum>>>();
    EXPECT_EQ(schema::Type::LIST, schema.whichElementType());
    EXPECT_NONFATAL_FAILURE(schema.getStructElementType());
    EXPECT_NONFATAL_FAILURE(schema.getEnumElementType());
    EXPECT_NONFATAL_FAILURE(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::ENUM, inner.whichElementType());
    EXPECT_TRUE(inner.getEnumElementType() == Schema::from<TestEnum>());
  }

  {
    auto context = Schema::from<TestAllTypes>();
    auto type = context.getFieldByName("enumList").getProto().getSlot().getType();

    ListSchema schema = ListSchema::of(type.getList().getElementType(), context);
    EXPECT_EQ(schema::Type::ENUM, schema.whichElementType());
    EXPECT_TRUE(schema.getEnumElementType() == Schema::from<TestEnum>());
    EXPECT_NONFATAL_FAILURE(schema.getStructElementType());
    EXPECT_NONFATAL_FAILURE(schema.getInterfaceElementType());
    EXPECT_NONFATAL_FAILURE(schema.getListElementType());
  }
}

TEST(Schema, UnnamedUnion) {
  StructSchema schema = Schema::from<test::TestUnnamedUnion>();

  EXPECT_TRUE(schema.findFieldByName("") == nullptr);

  EXPECT_TRUE(schema.findFieldByName("foo") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("bar") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("before") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("after") != nullptr);
}

TEST(Schema, NullSchemas) {
  EXPECT_EQ(0xff, (uint)Schema().getProto().which());
  EXPECT_TRUE(StructSchema().getProto().isStruct());
  EXPECT_TRUE(EnumSchema().getProto().isEnum());
  EXPECT_TRUE(InterfaceSchema().getProto().isInterface());
  EXPECT_TRUE(ConstSchema().getProto().isConst());

  EXPECT_EQ("(null schema)", Schema().getProto().getDisplayName());
  EXPECT_EQ("(null struct schema)", StructSchema().getProto().getDisplayName());
  EXPECT_EQ("(null enum schema)", EnumSchema().getProto().getDisplayName());
  EXPECT_EQ("(null interface schema)", InterfaceSchema().getProto().getDisplayName());
  EXPECT_EQ("(null const schema)", ConstSchema().getProto().getDisplayName());

  EXPECT_TRUE(Schema::from<Capability>() == InterfaceSchema());
  EXPECT_EQ(InterfaceSchema().getProto().getId(), typeId<Capability>());
}

TEST(Schema, Interfaces) {
  InterfaceSchema schema = Schema::from<test::TestMoreStuff>();

  EXPECT_EQ(typeId<test::TestMoreStuff>(), schema.getProto().getId());

  EXPECT_TRUE(schema.getDependency(typeId<test::TestCallOrder>()) ==
              Schema::from<test::TestCallOrder>());
  EXPECT_TRUE(schema.getDependency(typeId<test::TestCallOrder>()) != schema);
  EXPECT_NONFATAL_FAILURE(schema.getDependency(typeId<TestDefaults>()));

  EXPECT_TRUE(schema.asInterface() == schema);
  EXPECT_NONFATAL_FAILURE(schema.asStruct());
  EXPECT_NONFATAL_FAILURE(schema.asEnum());

  ASSERT_EQ(schema.getMethods().size(), schema.getProto().getInterface().getMethods().size());
  InterfaceSchema::Method method = schema.getMethods()[0];
  EXPECT_EQ("callFoo", method.getProto().getName());
  EXPECT_TRUE(method.getContainingInterface() == schema);

  InterfaceSchema::Method lookup = schema.getMethodByName("callFoo");
  EXPECT_TRUE(lookup == method);
  EXPECT_TRUE(lookup != schema.getMethods()[1]);

  EXPECT_TRUE(Schema::from<TestDefaults>().getFieldByName("int32Field")
      .getProto().getSlot().getHadExplicitDefault());

  EXPECT_TRUE(schema.findMethodByName("noSuchMethod") == nullptr);

  EXPECT_TRUE(schema.findMethodByName("callFooWhenResolved") != nullptr);
  EXPECT_TRUE(schema.findMethodByName("neverReturn") != nullptr);
  EXPECT_TRUE(schema.findMethodByName("hold") != nullptr);
  EXPECT_TRUE(schema.findMethodByName("callHeld") != nullptr);
  EXPECT_TRUE(schema.findMethodByName("getHeld") != nullptr);

  auto params = schema.getDependency(schema.getMethodByName("methodWithDefaults")
      .getProto().getParamStructType()).asStruct();
  EXPECT_FALSE(params.getFieldByName("a").getProto().getSlot().getHadExplicitDefault());
  EXPECT_TRUE(params.getFieldByName("b").getProto().getSlot().getHadExplicitDefault());
  EXPECT_TRUE(params.getFieldByName("c").getProto().getSlot().getHadExplicitDefault());
}

TEST(Schema, Generics) {
  StructSchema allTypes = Schema::from<TestAllTypes>();
  StructSchema tap = Schema::from<test::TestAnyPointer>();
  StructSchema schema = Schema::from<test::TestUseGenerics>();

  StructSchema branded;

  {
    StructSchema::Field basic = schema.getFieldByName("basic");
    branded = basic.getType().asStruct();

    StructSchema::Field foo = branded.getFieldByName("foo");
    EXPECT_TRUE(foo.getType().asStruct() == allTypes);
    EXPECT_TRUE(foo.getType().asStruct() != tap);

    StructSchema instance2 = branded.getFieldByName("rev").getType().asStruct();
    StructSchema::Field foo2 = instance2.getFieldByName("foo");
    EXPECT_TRUE(foo2.getType().asStruct() == tap);
    EXPECT_TRUE(foo2.getType().asStruct() != allTypes);
  }

  {
    StructSchema inner2 = schema.getFieldByName("inner2").getType().asStruct();

    StructSchema bound = inner2.getFieldByName("innerBound").getType().asStruct();
    Type boundFoo = bound.getFieldByName("foo").getType();
    EXPECT_FALSE(boundFoo.isAnyPointer());
    EXPECT_TRUE(boundFoo.asStruct() == allTypes);

    StructSchema unbound = inner2.getFieldByName("innerUnbound").getType().asStruct();
    Type unboundFoo = unbound.getFieldByName("foo").getType();
    EXPECT_TRUE(unboundFoo.isAnyPointer());
  }

  {
    InterfaceSchema cap = schema.getFieldByName("genericCap").getType().asInterface();
    InterfaceSchema::Method method = cap.getMethodByName("call");

    StructSchema inner2 = method.getParamType();
    StructSchema bound = inner2.getFieldByName("innerBound").getType().asStruct();
    Type boundFoo = bound.getFieldByName("foo").getType();
    EXPECT_FALSE(boundFoo.isAnyPointer());
    EXPECT_TRUE(boundFoo.asStruct() == allTypes);
    EXPECT_TRUE(inner2.getFieldByName("baz").getType().isText());

    StructSchema results = method.getResultType();
    EXPECT_TRUE(results.getFieldByName("qux").getType().isData());

    EXPECT_TRUE(results.getFieldByName("gen").getType().asStruct() == branded);
  }
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
