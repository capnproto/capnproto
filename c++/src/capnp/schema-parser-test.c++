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

#include "schema-parser.h"
#include <kj/compat/gtest.h>
#include "test-util.h"
#include <kj/debug.h>
#include <map>

namespace capnp {
namespace {

#if _WIN32
#define ABS(x) "C:\\" x
#else
#define ABS(x) "/" x
#endif

class FakeFileReader final: public kj::Filesystem {
public:
  void add(kj::StringPtr name, kj::StringPtr content) {
    root->openFile(cwd.evalNative(name), kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT)
        ->writeAll(content);
  }

  const kj::Directory& getRoot() const override { return *root; }
  const kj::Directory& getCurrent() const override { return *current; }
  kj::PathPtr getCurrentPath() const override { return cwd; }

private:
  kj::Own<const kj::Directory> root = kj::newInMemoryDirectory(kj::nullClock());
  kj::Path cwd = kj::Path({}).evalNative(ABS("path/to/current/dir"));
  kj::Own<const kj::Directory> current = root->openSubdir(cwd,
      kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT);
};

static uint64_t getFieldTypeFileId(StructSchema::Field field) {
  return field.getContainingStruct()
      .getDependency(field.getProto().getSlot().getType().getStruct().getTypeId())
      .getProto().getScopeId();
}

TEST(SchemaParser, Basic) {
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("src/foo/bar.capnp",
      "@0x8123456789abcdef;\n"
      "struct Bar {\n"
      "  baz @0: import \"baz.capnp\".Baz;\n"
      "  corge @1: import \"../qux/corge.capnp\".Corge;\n"
      "  grault @2: import \"/grault.capnp\".Grault;\n"
      "  garply @3: import \"/garply.capnp\".Garply;\n"
      "}\n");
  reader.add("src/foo/baz.capnp",
      "@0x823456789abcdef1;\n"
      "struct Baz {}\n");
  reader.add("src/qux/corge.capnp",
      "@0x83456789abcdef12;\n"
      "struct Corge {}\n");
  reader.add(ABS("usr/include/grault.capnp"),
      "@0x8456789abcdef123;\n"
      "struct Grault {}\n");
  reader.add(ABS("opt/include/grault.capnp"),
      "@0x8000000000000001;\n"
      "struct WrongGrault {}\n");
  reader.add(ABS("usr/local/include/garply.capnp"),
      "@0x856789abcdef1234;\n"
      "struct Garply {}\n");

  kj::StringPtr importPath[] = {
    ABS("usr/include"), ABS("usr/local/include"), ABS("opt/include")
  };

  ParsedSchema barSchema = parser.parseDiskFile(
      "foo2/bar2.capnp", "src/foo/bar.capnp", importPath);

  auto barProto = barSchema.getProto();
  EXPECT_EQ(0x8123456789abcdefull, barProto.getId());
  EXPECT_EQ("foo2/bar2.capnp", barProto.getDisplayName());

  auto barStruct = barSchema.getNested("Bar");
  auto barFields = barStruct.asStruct().getFields();
  ASSERT_EQ(4u, barFields.size());
  EXPECT_EQ("baz", barFields[0].getProto().getName());
  EXPECT_EQ(0x823456789abcdef1ull, getFieldTypeFileId(barFields[0]));
  EXPECT_EQ("corge", barFields[1].getProto().getName());
  EXPECT_EQ(0x83456789abcdef12ull, getFieldTypeFileId(barFields[1]));
  EXPECT_EQ("grault", barFields[2].getProto().getName());
  EXPECT_EQ(0x8456789abcdef123ull, getFieldTypeFileId(barFields[2]));
  EXPECT_EQ("garply", barFields[3].getProto().getName());
  EXPECT_EQ(0x856789abcdef1234ull, getFieldTypeFileId(barFields[3]));

  auto bazSchema = parser.parseDiskFile(
      "not/used/because/already/loaded",
      "src/foo/baz.capnp", importPath);
  EXPECT_EQ(0x823456789abcdef1ull, bazSchema.getProto().getId());
  EXPECT_EQ("foo2/baz.capnp", bazSchema.getProto().getDisplayName());
  auto bazStruct = bazSchema.getNested("Baz").asStruct();
  EXPECT_EQ(bazStruct, barStruct.getDependency(bazStruct.getProto().getId()));

  auto corgeSchema = parser.parseDiskFile(
      "not/used/because/already/loaded",
      "src/qux/corge.capnp", importPath);
  EXPECT_EQ(0x83456789abcdef12ull, corgeSchema.getProto().getId());
  EXPECT_EQ("qux/corge.capnp", corgeSchema.getProto().getDisplayName());
  auto corgeStruct = corgeSchema.getNested("Corge").asStruct();
  EXPECT_EQ(corgeStruct, barStruct.getDependency(corgeStruct.getProto().getId()));

  auto graultSchema = parser.parseDiskFile(
      "not/used/because/already/loaded",
      ABS("usr/include/grault.capnp"), importPath);
  EXPECT_EQ(0x8456789abcdef123ull, graultSchema.getProto().getId());
  EXPECT_EQ("grault.capnp", graultSchema.getProto().getDisplayName());
  auto graultStruct = graultSchema.getNested("Grault").asStruct();
  EXPECT_EQ(graultStruct, barStruct.getDependency(graultStruct.getProto().getId()));

  // Try importing the other grault.capnp directly.  It'll get the display name we specify since
  // it wasn't imported before.
  auto wrongGraultSchema = parser.parseDiskFile(
      "weird/display/name.capnp",
      ABS("opt/include/grault.capnp"), importPath);
  EXPECT_EQ(0x8000000000000001ull, wrongGraultSchema.getProto().getId());
  EXPECT_EQ("weird/display/name.capnp", wrongGraultSchema.getProto().getDisplayName());
}

TEST(SchemaParser, Constants) {
  // This is actually a test of the full dynamic API stack for constants, because the schemas for
  // constants are not actually accessible from the generated code API, so the only way to ever
  // get a ConstSchema is by parsing it.

  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("const.capnp",
      "@0x8123456789abcdef;\n"
      "const uint32Const :UInt32 = 1234;\n"
      "const listConst :List(Float32) = [1.25, 2.5, 3e4];\n"
      "const structConst :Foo = (bar = 123, baz = \"qux\");\n"
      "struct Foo {\n"
      "  bar @0 :Int16;\n"
      "  baz @1 :Text;\n"
      "}\n"
      "const genericConst :TestGeneric(Text) = (value = \"text\");\n"
      "struct TestGeneric(T) {\n"
      "  value @0 :T;\n"
      "}\n");

  ParsedSchema fileSchema = parser.parseDiskFile(
      "const.capnp", "const.capnp", nullptr);

  EXPECT_EQ(1234, fileSchema.getNested("uint32Const").asConst().as<uint32_t>());

  auto list = fileSchema.getNested("listConst").asConst().as<DynamicList>();
  ASSERT_EQ(3u, list.size());
  EXPECT_EQ(1.25, list[0].as<float>());
  EXPECT_EQ(2.5, list[1].as<float>());
  EXPECT_EQ(3e4f, list[2].as<float>());

  auto structConst = fileSchema.getNested("structConst").asConst().as<DynamicStruct>();
  EXPECT_EQ(123, structConst.get("bar").as<int16_t>());
  EXPECT_EQ("qux", structConst.get("baz").as<Text>());

  auto genericConst = fileSchema.getNested("genericConst").asConst().as<DynamicStruct>();
  EXPECT_EQ("text", genericConst.get("value").as<Text>());
}

void expectSourceInfo(schema::Node::SourceInfo::Reader sourceInfo,
                   uint64_t expectedId, kj::StringPtr expectedComment,
                   std::initializer_list<const kj::StringPtr> expectedMembers) {
  KJ_EXPECT(sourceInfo.getId() == expectedId, sourceInfo, expectedId);
  KJ_EXPECT(sourceInfo.getDocComment() == expectedComment, sourceInfo, expectedComment);

  auto members = sourceInfo.getMembers();
  KJ_ASSERT(members.size() == expectedMembers.size());
  for (auto i: kj::indices(expectedMembers)) {
    KJ_EXPECT(members[i].getDocComment() == expectedMembers.begin()[i],
              members[i], expectedMembers.begin()[i]);
  }
}

TEST(SchemaParser, SourceInfo) {
  FakeFileReader reader;
  SchemaParser parser;
  parser.setDiskFilesystem(reader);

  reader.add("foo.capnp",
      "@0x84a2c6051e1061ed;\n"
      "# file doc comment\n"
      "\n"
      "struct Foo @0xc6527d0a670dc4c3 {\n"
      "  # struct doc comment\n"
      "  # second line\n"
      "\n"
      "  bar @0 :UInt32;\n"
      "  # field doc comment\n"
      "  baz :group {\n"
      "    # group doc comment\n"
      "    qux @1 :Text;\n"
      "    # group field doc comment\n"
      "  }\n"
      "}\n"
      "\n"
      "enum Corge @0xae08878f1a016f14 {\n"
      "  # enum doc comment\n"
      "  grault @0;\n"
      "  # enumerant doc comment\n"
      "  garply @1;\n"
      "}\n"
      "\n"
      "interface Waldo @0xc0f1b0aff62b761e {\n"
      "  # interface doc comment\n"
      "  fred @0 (plugh :Int32) -> (xyzzy :Text);\n"
      "  # method doc comment\n"
      "}\n"
      "\n"
      "struct Thud @0xcca9972702b730b4 {}\n"
      "# post-comment\n");

  ParsedSchema file = parser.parseDiskFile(
      "foo.capnp", "foo.capnp", nullptr);
  ParsedSchema foo = file.getNested("Foo");

  expectSourceInfo(file.getSourceInfo(), 0x84a2c6051e1061edull, "file doc comment\n", {});

  expectSourceInfo(foo.getSourceInfo(), 0xc6527d0a670dc4c3ull, "struct doc comment\nsecond line\n",
      { "field doc comment\n", "group doc comment\n" });

  auto group = foo.asStruct().getFieldByName("baz").getType().asStruct();
  expectSourceInfo(KJ_ASSERT_NONNULL(parser.getSourceInfo(group)),
      group.getProto().getId(), "group doc comment\n", { "group field doc comment\n" });

  ParsedSchema corge = file.getNested("Corge");
  expectSourceInfo(corge.getSourceInfo(), 0xae08878f1a016f14, "enum doc comment\n",
      { "enumerant doc comment\n", "" });

  ParsedSchema waldo = file.getNested("Waldo");
  expectSourceInfo(waldo.getSourceInfo(), 0xc0f1b0aff62b761e, "interface doc comment\n",
      { "method doc comment\n" });

  ParsedSchema thud = file.getNested("Thud");
  expectSourceInfo(thud.getSourceInfo(), 0xcca9972702b730b4, "post-comment\n", {});
}

}  // namespace
}  // namespace capnp
