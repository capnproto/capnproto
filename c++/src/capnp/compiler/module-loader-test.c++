// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:

#include "module-loader.h"
#include "error-reporter.h"
#include <kj/compat/gtest.h>
#include <kj/filesystem.h>
#include <kj/test.h>
#include <cstdlib>

namespace capnp {
namespace compiler {
namespace {

class NullErrorReporter final: public GlobalErrorReporter {
public:
  void addError(const kj::ReadableDirectory&, kj::PathPtr,
                SourcePos, SourcePos, kj::StringPtr) override {
    KJ_FAIL_ASSERT("unexpected parse error");
  }

  bool hadErrors() override { return false; }
};

TEST(ModuleLoader, DuplicateFileAtDifferentPaths) {
  auto filesystem = kj::newDiskFilesystem();
  auto testDir = getenv("TEST_TMPDIR");
  KJ_ASSERT(testDir != nullptr);
  auto testPath = kj::Path(nullptr).evalNative(testDir);
  auto root = filesystem->getRoot().openSubdir(testPath, kj::WriteMode::MODIFY);
  root->openFile(kj::Path::parse("nested/schema.capnp"),
                 kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT)
      ->writeAll("@0xabcdefabcdefabcd;\n");
  auto nested = root->openSubdir(kj::Path("nested"));

  NullErrorReporter errorReporter;
  ModuleLoader loader(errorReporter);
  auto& first = KJ_ASSERT_NONNULL(loader.loadModule(
      *root, kj::Path::parse("nested/schema.capnp")));

  KJ_EXPECT_LOG(WARNING, "same source file mapped at two different paths");
  auto& second = KJ_ASSERT_NONNULL(loader.loadModule(*nested, kj::Path("schema.capnp")));

  EXPECT_EQ(&first, &second);
}

}  // namespace
}  // namespace compiler
}  // namespace capnp
