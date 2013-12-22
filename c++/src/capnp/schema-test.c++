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

#include "schema.h"
#include <gtest/gtest.h>
#include "test-util.h"

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
  EXPECT_ANY_THROW(schema.getDependency(typeId<TestDefaults>()));

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

  EXPECT_ANY_THROW(schema.getDependency(typeId<TestAllTypes>()));
  EXPECT_ANY_THROW(schema.getDependency(typeId<TestEnum>()));

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

  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getStructElementType());
  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getEnumElementType());
  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getInterfaceElementType());
  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getListElementType());

  {
    ListSchema schema = Schema::from<List<TestAllTypes>>();
    EXPECT_EQ(schema::Type::STRUCT, schema.whichElementType());
    EXPECT_TRUE(schema.getStructElementType() == Schema::from<TestAllTypes>());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());
    EXPECT_ANY_THROW(schema.getListElementType());
  }

  {
    ListSchema schema = Schema::from<List<TestEnum>>();
    EXPECT_EQ(schema::Type::ENUM, schema.whichElementType());
    EXPECT_TRUE(schema.getEnumElementType() == Schema::from<TestEnum>());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());
    EXPECT_ANY_THROW(schema.getListElementType());
  }

  // TODO(someday):  Test interfaces.

  {
    ListSchema schema = Schema::from<List<List<int32_t>>>();
    EXPECT_EQ(schema::Type::LIST, schema.whichElementType());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::INT32, inner.whichElementType());
  }

  {
    ListSchema schema = Schema::from<List<List<TestAllTypes>>>();
    EXPECT_EQ(schema::Type::LIST, schema.whichElementType());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::STRUCT, inner.whichElementType());
    EXPECT_TRUE(inner.getStructElementType() == Schema::from<TestAllTypes>());
  }

  {
    ListSchema schema = Schema::from<List<List<TestEnum>>>();
    EXPECT_EQ(schema::Type::LIST, schema.whichElementType());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());

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
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());
    EXPECT_ANY_THROW(schema.getListElementType());
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
  EXPECT_ANY_THROW(schema.getDependency(typeId<TestDefaults>()));

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

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
