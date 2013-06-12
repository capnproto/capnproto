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

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#endif

TEST(Schema, Structs) {
  StructSchema schema = Schema::from<TestAllTypes>();

  EXPECT_EQ(typeId<TestAllTypes>(), schema.getProto().getId());

  EXPECT_TRUE(schema.getDependency(typeId<TestEnum>()) == Schema::from<TestEnum>());
  EXPECT_TRUE(schema.getDependency(typeId<TestEnum>()) != schema);
  EXPECT_TRUE(schema.getDependency(typeId<TestAllTypes>()) == Schema::from<TestAllTypes>());
  EXPECT_TRUE(schema.getDependency(typeId<TestAllTypes>()) == schema);
  EXPECT_ANY_THROW(schema.getDependency(typeId<TestDefaults>()));

  EXPECT_TRUE(schema.asStruct() == schema);
  EXPECT_ANY_THROW(schema.asEnum());
  EXPECT_ANY_THROW(schema.asInterface());

  ASSERT_EQ(schema.getMembers().size(),
            schema.getProto().getBody().getStructNode().getMembers().size());
  StructSchema::Member member = schema.getMembers()[0];
  EXPECT_EQ("voidField", member.getProto().getName());
  EXPECT_TRUE(member.getContainingStruct() == schema);
  EXPECT_TRUE(member.getContainingUnion() == nullptr);

  EXPECT_ANY_THROW(member.asUnion());

  StructSchema::Member lookup = schema.getMemberByName("voidField");
  EXPECT_TRUE(lookup == member);
  EXPECT_TRUE(lookup != schema.getMembers()[1]);

  EXPECT_TRUE(schema.findMemberByName("noSuchField") == nullptr);

  EXPECT_TRUE(schema.findMemberByName("int32Field") != nullptr);
  EXPECT_TRUE(schema.findMemberByName("float32List") != nullptr);
  EXPECT_TRUE(schema.findMemberByName("dataList") != nullptr);
  EXPECT_TRUE(schema.findMemberByName("textField") != nullptr);
  EXPECT_TRUE(schema.findMemberByName("structField") != nullptr);
}

TEST(Schema, FieldLookupOutOfOrder) {
  // Tests that name lookup works correctly when the fields are defined out-of-order in the schema
  // file.
  auto schema = Schema::from<test::TestOutOfOrder>().asStruct();

  EXPECT_EQ("qux", schema.getMembers()[0].getProto().getName());
  EXPECT_EQ("grault", schema.getMembers()[1].getProto().getName());
  EXPECT_EQ("bar", schema.getMembers()[2].getProto().getName());
  EXPECT_EQ("foo", schema.getMembers()[3].getProto().getName());
  EXPECT_EQ("corge", schema.getMembers()[4].getProto().getName());
  EXPECT_EQ("waldo", schema.getMembers()[5].getProto().getName());
  EXPECT_EQ("quux", schema.getMembers()[6].getProto().getName());
  EXPECT_EQ("garply", schema.getMembers()[7].getProto().getName());
  EXPECT_EQ("baz", schema.getMembers()[8].getProto().getName());

  EXPECT_EQ(3, schema.getMemberByName("foo").getProto().getOrdinal());
  EXPECT_EQ(2, schema.getMemberByName("bar").getProto().getOrdinal());
  EXPECT_EQ(8, schema.getMemberByName("baz").getProto().getOrdinal());
  EXPECT_EQ(0, schema.getMemberByName("qux").getProto().getOrdinal());
  EXPECT_EQ(6, schema.getMemberByName("quux").getProto().getOrdinal());
  EXPECT_EQ(4, schema.getMemberByName("corge").getProto().getOrdinal());
  EXPECT_EQ(1, schema.getMemberByName("grault").getProto().getOrdinal());
  EXPECT_EQ(7, schema.getMemberByName("garply").getProto().getOrdinal());
  EXPECT_EQ(5, schema.getMemberByName("waldo").getProto().getOrdinal());
}

TEST(Schema, Unions) {
  auto schema = Schema::from<TestUnion>().asStruct();

  EXPECT_TRUE(schema.findMemberByName("bit0") != nullptr);
  EXPECT_TRUE(schema.findMemberByName("u1f0s8") == nullptr);

  auto union1 = schema.getMemberByName("union1").asUnion();
  EXPECT_TRUE(union1.findMemberByName("bin0") == nullptr);
  EXPECT_TRUE(union1.getContainingUnion() == nullptr);

  auto u1f0s8 = union1.getMemberByName("u1f0s8");
  EXPECT_EQ("u1f0s8", u1f0s8.getProto().getName());
  EXPECT_TRUE(u1f0s8.getContainingStruct() == schema);
  KJ_IF_MAYBE(containing, u1f0s8.getContainingUnion()) {
    EXPECT_TRUE(*containing == union1);
  } else {
    ADD_FAILURE() << "u1f0s8.getContainingUnion() returned null";
  }

  EXPECT_TRUE(union1.findMemberByName("u1f1s8") != nullptr);
  EXPECT_TRUE(union1.findMemberByName("u1f0s32") != nullptr);
  EXPECT_TRUE(union1.findMemberByName("u1f0sp") != nullptr);
  EXPECT_TRUE(union1.findMemberByName("u1f1s1") != nullptr);

  EXPECT_TRUE(union1.findMemberByName("u0f0s1") == nullptr);
  EXPECT_TRUE(union1.findMemberByName("u2f0s8") == nullptr);
  EXPECT_TRUE(union1.findMemberByName("noSuchField") == nullptr);
}

TEST(Schema, Enums) {
  EnumSchema schema = Schema::from<TestEnum>();

  EXPECT_EQ(typeId<TestEnum>(), schema.getProto().getId());

  EXPECT_ANY_THROW(schema.getDependency(typeId<TestAllTypes>()));
  EXPECT_ANY_THROW(schema.getDependency(typeId<TestEnum>()));

  EXPECT_ANY_THROW(schema.asStruct());
  EXPECT_ANY_THROW(schema.asInterface());
  EXPECT_TRUE(schema.asEnum() == schema);

  ASSERT_EQ(schema.getEnumerants().size(),
            schema.getProto().getBody().getEnumNode().getEnumerants().size());
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
  EXPECT_EQ(schema::Type::Body::VOID_TYPE, Schema::from<List<Void>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::BOOL_TYPE, Schema::from<List<bool>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::INT8_TYPE, Schema::from<List<int8_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::INT16_TYPE, Schema::from<List<int16_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::INT32_TYPE, Schema::from<List<int32_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::INT64_TYPE, Schema::from<List<int64_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::UINT8_TYPE, Schema::from<List<uint8_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::UINT16_TYPE, Schema::from<List<uint16_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::UINT32_TYPE, Schema::from<List<uint32_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::UINT64_TYPE, Schema::from<List<uint64_t>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::FLOAT32_TYPE, Schema::from<List<float>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::FLOAT64_TYPE, Schema::from<List<double>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::TEXT_TYPE, Schema::from<List<Text>>().whichElementType());
  EXPECT_EQ(schema::Type::Body::DATA_TYPE, Schema::from<List<Data>>().whichElementType());

  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getStructElementType());
  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getEnumElementType());
  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getInterfaceElementType());
  EXPECT_ANY_THROW(Schema::from<List<uint16_t>>().getListElementType());

  {
    ListSchema schema = Schema::from<List<TestAllTypes>>();
    EXPECT_EQ(schema::Type::Body::STRUCT_TYPE, schema.whichElementType());
    EXPECT_TRUE(schema.getStructElementType() == Schema::from<TestAllTypes>());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());
    EXPECT_ANY_THROW(schema.getListElementType());
  }

  {
    ListSchema schema = Schema::from<List<TestEnum>>();
    EXPECT_EQ(schema::Type::Body::ENUM_TYPE, schema.whichElementType());
    EXPECT_TRUE(schema.getEnumElementType() == Schema::from<TestEnum>());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());
    EXPECT_ANY_THROW(schema.getListElementType());
  }

  // TODO(someday):  Test interfaces.

  {
    ListSchema schema = Schema::from<List<List<int32_t>>>();
    EXPECT_EQ(schema::Type::Body::LIST_TYPE, schema.whichElementType());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::Body::INT32_TYPE, inner.whichElementType());
  }

  {
    ListSchema schema = Schema::from<List<List<TestAllTypes>>>();
    EXPECT_EQ(schema::Type::Body::LIST_TYPE, schema.whichElementType());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::Body::STRUCT_TYPE, inner.whichElementType());
    EXPECT_TRUE(inner.getStructElementType() == Schema::from<TestAllTypes>());
  }

  {
    ListSchema schema = Schema::from<List<List<TestEnum>>>();
    EXPECT_EQ(schema::Type::Body::LIST_TYPE, schema.whichElementType());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getEnumElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());

    ListSchema inner = schema.getListElementType();
    EXPECT_EQ(schema::Type::Body::ENUM_TYPE, inner.whichElementType());
    EXPECT_TRUE(inner.getEnumElementType() == Schema::from<TestEnum>());
  }

  {
    auto context = Schema::from<TestAllTypes>();
    auto type = context.getMemberByName("enumList").getProto().getBody().getFieldMember().getType();

    ListSchema schema = ListSchema::of(type.getBody().getListType(), context);
    EXPECT_EQ(schema::Type::Body::ENUM_TYPE, schema.whichElementType());
    EXPECT_TRUE(schema.getEnumElementType() == Schema::from<TestEnum>());
    EXPECT_ANY_THROW(schema.getStructElementType());
    EXPECT_ANY_THROW(schema.getInterfaceElementType());
    EXPECT_ANY_THROW(schema.getListElementType());
  }
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
