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

#include "schema-loader.h"
#include <kj/compat/gtest.h>
#include "test-util.h"
#include <kj/debug.h>

namespace capnp {
namespace _ {  // private
namespace {

TEST(SchemaLoader, Load) {
  SchemaLoader loader;

  Schema struct32Schema = loader.load(Schema::from<test::TestLists::Struct32>().getProto());

  auto nativeSchema = Schema::from<test::TestLists>();
  Schema testListsSchema = loader.load(nativeSchema.getProto());

  Schema struct8Schema = loader.load(Schema::from<test::TestLists::Struct8>().getProto());
  Schema structPSchema = loader.load(Schema::from<test::TestLists::StructP>().getProto());

  EXPECT_EQ(kj::str(nativeSchema.getProto()), kj::str(testListsSchema.getProto()));

  EXPECT_FALSE(testListsSchema == nativeSchema);
  EXPECT_FALSE(struct32Schema == Schema::from<test::TestLists::Struct32>());
  EXPECT_FALSE(struct8Schema == Schema::from<test::TestLists::Struct8>());
  EXPECT_FALSE(structPSchema == Schema::from<test::TestLists::StructP>());

  EXPECT_TRUE(testListsSchema.getDependency(typeId<test::TestLists::Struct32>()) == struct32Schema);
  EXPECT_TRUE(testListsSchema.getDependency(typeId<test::TestLists::Struct8>()) == struct8Schema);
  EXPECT_TRUE(testListsSchema.getDependency(typeId<test::TestLists::StructP>()) == structPSchema);

  auto struct16Schema = testListsSchema.getDependency(typeId<test::TestLists::Struct16>());
  EXPECT_EQ(0u, struct16Schema.getProto().getStruct().getFields().size());
}

TEST(SchemaLoader, LoadLateUnion) {
  SchemaLoader loader;

  StructSchema schema =
      loader.load(Schema::from<test::TestLateUnion>().getProto()).asStruct();
  loader.load(Schema::from<test::TestLateUnion::TheUnion>().getProto()).asStruct();
  loader.load(Schema::from<test::TestLateUnion::AnotherUnion>().getProto()).asStruct();

  EXPECT_EQ(6,
      schema.getDependency(schema.getFieldByName("theUnion").getProto().getGroup().getTypeId())
            .asStruct().getFieldByName("grault").getProto().getOrdinal().getExplicit());
  EXPECT_EQ(9,
      schema.getDependency(schema.getFieldByName("anotherUnion").getProto().getGroup().getTypeId())
            .asStruct().getFieldByName("corge").getProto().getOrdinal().getExplicit());
  EXPECT_TRUE(schema.findFieldByName("corge") == nullptr);
  EXPECT_TRUE(schema.findFieldByName("grault") == nullptr);
}

TEST(SchemaLoader, LoadUnnamedUnion) {
  SchemaLoader loader;

  StructSchema schema =
      loader.load(Schema::from<test::TestUnnamedUnion>().getProto()).asStruct();

  EXPECT_TRUE(schema.findFieldByName("") == nullptr);

  EXPECT_TRUE(schema.findFieldByName("foo") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("bar") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("before") != nullptr);
  EXPECT_TRUE(schema.findFieldByName("after") != nullptr);
}

TEST(SchemaLoader, Use) {
  SchemaLoader loader;

  StructSchema schema = loader.load(Schema::from<TestAllTypes>().getProto()).asStruct();

  // Also have to load TestEnum.
  loader.load(Schema::from<TestEnum>().getProto());

  {
    MallocMessageBuilder builder;
    auto root = builder.getRoot<DynamicStruct>(schema);

    initDynamicTestMessage(root);
    checkDynamicTestMessage(root.asReader());

    // Can't convert to TestAllTypes because we didn't use loadCompiledTypeAndDependencies().
    EXPECT_ANY_THROW(root.as<TestAllTypes>());

    // But if we reinterpret the raw bytes, it works.
    checkTestMessage(builder.getRoot<TestAllTypes>());
  }

  loader.loadCompiledTypeAndDependencies<TestAllTypes>();

  {
    MallocMessageBuilder builder;
    auto root = builder.getRoot<DynamicStruct>(schema);

    initDynamicTestMessage(root);

    // Now we can actually cast.
    checkTestMessage(root.as<TestAllTypes>());
  }

  // Let's also test TestListDefaults, but as we do so, let's load the compiled types first, to
  // make sure the opposite order works.

  loader.loadCompiledTypeAndDependencies<TestListDefaults>();
  StructSchema testListsSchema = loader.get(typeId<TestListDefaults>()).asStruct();
  EXPECT_TRUE(testListsSchema != Schema::from<TestListDefaults>());

  {
    MallocMessageBuilder builder;
    auto root = builder.getRoot<DynamicStruct>(testListsSchema);

    initDynamicTestLists(root);
    checkDynamicTestLists(root.asReader());

    checkTestMessage(root.as<TestListDefaults>());
  }

  EXPECT_TRUE(loader.load(Schema::from<TestListDefaults>().getProto()) == testListsSchema);

  {
    MallocMessageBuilder builder;
    auto root = builder.getRoot<DynamicStruct>(testListsSchema);

    initDynamicTestLists(root);
    checkTestMessage(root.as<TestListDefaults>());
  }

  // Finally, let's test some unions.
  StructSchema unionSchema = loader.load(Schema::from<TestUnion>().getProto()).asStruct();
  loader.load(Schema::from<TestUnion::Union0>().getProto());
  loader.load(Schema::from<TestUnion::Union1>().getProto());
  {
    MallocMessageBuilder builder;
    auto root = builder.getRoot<DynamicStruct>(unionSchema);

    root.get("union0").as<DynamicStruct>().set("u0f1s16", 123);
    root.get("union1").as<DynamicStruct>().set("u1f0sp", "hello");

    auto reader = builder.getRoot<TestUnion>().asReader();
    EXPECT_EQ(123, reader.getUnion0().getU0f1s16());
    EXPECT_EQ("hello", reader.getUnion1().getU1f0sp());
  }
}

template <typename T>
Schema loadUnderAlternateTypeId(SchemaLoader& loader, uint64_t id) {
  MallocMessageBuilder schemaBuilder;
  schemaBuilder.setRoot(Schema::from<T>().getProto());
  auto root = schemaBuilder.getRoot<schema::Node>();
  root.setId(id);

  if (root.isStruct()) {
    // If the struct contains any self-referential members, change their type IDs as well.
    auto fields = root.getStruct().getFields();
    for (auto field: fields) {
      if (field.isSlot()) {
        auto type = field.getSlot().getType();
        if (type.isStruct() && type.getStruct().getTypeId() == typeId<T>()) {
          type.getStruct().setTypeId(id);
        }
      }
    }
  }

  return loader.load(root);
}

TEST(SchemaLoader, Upgrade) {
  SchemaLoader loader;

  loader.loadCompiledTypeAndDependencies<test::TestOldVersion>();

  StructSchema schema = loader.get(typeId<test::TestOldVersion>()).asStruct();

  EXPECT_EQ(kj::str(Schema::from<test::TestOldVersion>().getProto()),
            kj::str(schema.getProto()));

  loadUnderAlternateTypeId<test::TestNewVersion>(loader, typeId<test::TestOldVersion>());

  // The new version replaced the old.
  EXPECT_EQ(Schema::from<test::TestNewVersion>().getProto().getDisplayName(),
            schema.getProto().getDisplayName());

  // But it is still usable as the old version.
  schema.requireUsableAs<test::TestOldVersion>();
}

TEST(SchemaLoader, Downgrade) {
  SchemaLoader loader;

  loader.loadCompiledTypeAndDependencies<test::TestNewVersion>();

  StructSchema schema = loader.get(typeId<test::TestNewVersion>()).asStruct();

  EXPECT_EQ(kj::str(Schema::from<test::TestNewVersion>().getProto()), kj::str(schema.getProto()));

  loadUnderAlternateTypeId<test::TestOldVersion>(loader, typeId<test::TestNewVersion>());

  // We kept the new version, because the replacement was older.
  EXPECT_EQ(Schema::from<test::TestNewVersion>().getProto().getDisplayName(),
            schema.getProto().getDisplayName());
  schema.requireUsableAs<test::TestNewVersion>();
}

TEST(SchemaLoader, Incompatible) {
  SchemaLoader loader;
  loader.loadCompiledTypeAndDependencies<test::TestListDefaults>();
  EXPECT_NONFATAL_FAILURE(
      loadUnderAlternateTypeId<test::TestAllTypes>(loader, typeId<test::TestListDefaults>()));
}

TEST(SchemaLoader, Enumerate) {
  SchemaLoader loader;
  loader.loadCompiledTypeAndDependencies<TestAllTypes>();
  auto list = loader.getAllLoaded();

  ASSERT_EQ(2u, list.size());
  if (list[0] == loader.get(typeId<TestAllTypes>())) {
    EXPECT_TRUE(list[1] == loader.get(typeId<TestEnum>()));
  } else {
    EXPECT_TRUE(list[0] == loader.get(typeId<TestEnum>()));
    EXPECT_TRUE(list[1] == loader.get(typeId<TestAllTypes>()));
  }
}

TEST(SchemaLoader, EnumerateNoPlaceholders) {
  SchemaLoader loader;
  Schema schema = loader.load(Schema::from<TestDefaults>().getProto());

  {
    auto list = loader.getAllLoaded();
    ASSERT_EQ(1u, list.size());
    EXPECT_TRUE(list[0] == schema);
  }

  Schema dep = schema.getDependency(typeId<TestAllTypes>());

  {
    auto list = loader.getAllLoaded();
    ASSERT_EQ(2u, list.size());
    if (list[0] == schema) {
      EXPECT_TRUE(list[1] == dep);
    } else {
      EXPECT_TRUE(list[0] == dep);
      EXPECT_TRUE(list[1] == schema);
    }
  }
}

class FakeLoaderCallback: public SchemaLoader::LazyLoadCallback {
public:
  FakeLoaderCallback(const schema::Node::Reader node): node(node), loaded(false) {}

  bool isLoaded() { return loaded; }

  void load(const SchemaLoader& loader, uint64_t id) const override {
    if (id == 1234) {
      // Magic "not found" ID.
      return;
    }

    EXPECT_EQ(node.getId(), id);
    EXPECT_FALSE(loaded);
    loaded = true;
    loader.loadOnce(node);
  }

private:
  const schema::Node::Reader node;
  mutable bool loaded = false;
};

TEST(SchemaLoader, LazyLoad) {
  FakeLoaderCallback callback(Schema::from<TestAllTypes>().getProto());
  SchemaLoader loader(callback);

  EXPECT_TRUE(loader.tryGet(1234) == nullptr);

  EXPECT_FALSE(callback.isLoaded());
  Schema schema = loader.get(typeId<TestAllTypes>());
  EXPECT_TRUE(callback.isLoaded());

  EXPECT_EQ(schema.getProto().getDisplayName(),
            Schema::from<TestAllTypes>().getProto().getDisplayName());

  EXPECT_EQ(schema, schema.getDependency(typeId<TestAllTypes>()));
  EXPECT_EQ(schema, loader.get(typeId<TestAllTypes>()));
}

TEST(SchemaLoader, LazyLoadGetDependency) {
  FakeLoaderCallback callback(Schema::from<TestAllTypes>().getProto());
  SchemaLoader loader(callback);

  Schema schema = loader.load(Schema::from<TestDefaults>().getProto());

  EXPECT_FALSE(callback.isLoaded());

  Schema dep = schema.getDependency(typeId<TestAllTypes>());

  EXPECT_TRUE(callback.isLoaded());

  EXPECT_EQ(dep.getProto().getDisplayName(),
            Schema::from<TestAllTypes>().getProto().getDisplayName());

  EXPECT_EQ(dep, schema.getDependency(typeId<TestAllTypes>()));
  EXPECT_EQ(dep, loader.get(typeId<TestAllTypes>()));
}

TEST(SchemaLoader, Generics) {
  SchemaLoader loader;

  StructSchema allTypes = loader.load(Schema::from<TestAllTypes>().getProto()).asStruct();
  StructSchema tap = loader.load(Schema::from<test::TestAnyPointer>().getProto()).asStruct();
  loader.load(Schema::from<test::TestGenerics<>::Inner>().getProto());
  loader.load(Schema::from<test::TestGenerics<>::Inner2<>>().getProto());
  loader.load(Schema::from<test::TestGenerics<>::Interface<>>().getProto());
  loader.load(Schema::from<test::TestGenerics<>::Interface<>::CallResults>().getProto());
  loader.load(Schema::from<test::TestGenerics<>>().getProto());
  StructSchema schema = loader.load(Schema::from<test::TestUseGenerics>().getProto()).asStruct();

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
